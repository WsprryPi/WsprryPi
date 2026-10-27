#!/usr/bin/env python3
"""Force the actual logger's predicate-to-wait shutdown window on Linux."""
import os
import re
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
WRAPPER = r'''
#define _GNU_SOURCE
#include <dlfcn.h>
#include <pthread.h>
#include <stdatomic.h>
#include <time.h>
static atomic_int entered;
int test_wait_entered(void) { return atomic_load(&entered); }
int pthread_cond_wait(pthread_cond_t *cv, pthread_mutex_t *mutex) {
    int (*real_wait)(pthread_cond_t *, pthread_mutex_t *) = dlsym(RTLD_NEXT, "pthread_cond_wait");
    atomic_store(&entered, 1);
    /* Keep the production predicate mutex held across this forced window. */
    struct timespec pause = {0, 150000000};
    nanosleep(&pause, 0);
    return real_wait(cv, mutex);
}
'''
FIXTURE = r'''
#include "lcblog.hpp"
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <dlfcn.h>
#include <thread>
static std::atomic<unsigned> records{0};
static bool sink(int, const std::string&, const std::string&) { ++records; return true; }
static void drained() { std::cout << "SINK_COUNT=" << records << "\n"; }
int main(int argc, char**) {
    if (argc > 1) std::atexit(drained); // Runs after the logger's destructor.
    auto& logger = getLogger();
    LCBLog::setJournaldSink(sink);
    logger.enableJournald(true);
    if (argc > 1) {
        for (unsigned i = 0; i < 100; ++i) {
            logger.logS(INFO, "drain-" + std::to_string(i * 2));
            logger.logS(ERROR, "drain-" + std::to_string(i * 2 + 1));
        }
    } else {
        auto entered = reinterpret_cast<int (*)()>(dlsym(RTLD_DEFAULT, "test_wait_entered"));
        if (!entered) return 4;
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
        while (!entered()) {
            if (std::chrono::steady_clock::now() >= deadline) return 5;
            std::this_thread::yield();
        }
    }
    return 0;
}
'''
with tempfile.TemporaryDirectory(prefix='wsprrypi-logger-shutdown-') as directory:
    work = Path(directory)
    (work/'wait.c').write_text(WRAPPER)
    (work/'fixture.cpp').write_text(FIXTURE)
    subprocess.run([os.environ.get('CC', 'cc'), '-shared', '-fPIC', '-Wall', '-Wextra', '-Werror',
                    str(work/'wait.c'), '-ldl', '-pthread', '-o', str(work/'wait.so')], check=True)
    libs = subprocess.check_output(['pkg-config', '--cflags', '--libs', 'libsystemd'], text=True).split()
    subprocess.run([os.environ.get('CXX', 'c++'), '-std=c++20', '-Wall', '-Wextra', '-Werror',
                    '-pthread', '-I'+str(ROOT/'src/LCBLog/src'), str(work/'fixture.cpp'),
                    str(ROOT/'src/LCBLog/src/lcblog.cpp'), '-ldl', *libs,
                    '-o', str(work/'fixture')], check=True)
    for attempt in range(5):
        subprocess.run([str(work/'fixture')], env=dict(os.environ, LD_PRELOAD=str(work/'wait.so')),
                       check=True, timeout=4, capture_output=True)
        result = subprocess.run([str(work/'fixture'), 'drain'], check=True, timeout=4,
                                capture_output=True, text=True)
        # A worker already waiting in stream mode may validly drain there while
        # journald is being enabled. Count both sinks rather than assuming the
        # mode transition retroactively changes the worker's current batch.
        count = re.findall(r'SINK_COUNT=(\d+)', result.stdout)
        streamed = re.findall(r'drain-(\d+)', result.stdout + result.stderr)
        assert len(count) == 1 and len(streamed) == len(set(streamed)), result
        assert int(count[0]) + len(streamed) == 200, result
print('10 actual logger forced-wait shutdown and pending-record drain checks passed')
