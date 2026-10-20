#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <openssl/ssl.h>
#include <pthread.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* Opt-in acceptance observer; never linked into or installed with the app.
 * Records write entry and successful plaintext I/O, with connection and
 * monotonic identities. Logging is synchronous and its cost belongs to the
 * observed host workload. A failed/truncated log terminates this private app;
 * it is never evidence that the independently timed Pico output is inactive. */
static pthread_once_t once = PTHREAD_ONCE_INIT;
static pthread_mutex_t records = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t identities = PTHREAD_MUTEX_INITIALIZER;
static struct { SSL *ssl; uint64_t id; } connections[64];
static uint64_t next_id, sequence;
static int output = -1;
static SSL *(*next_new)(SSL_CTX *);
static void (*next_free)(SSL *);
static int (*next_read)(SSL *, void *, size_t, size_t *);
static int (*next_write)(SSL *, const void *, size_t, size_t *);
static int (*next_handshake)(SSL *);

static uint64_t stamp(clockid_t clock) {
    struct timespec t;
    if (clock_gettime(clock, &t)) _exit(91);
    return (uint64_t)t.tv_sec * UINT64_C(1000000000) + (uint64_t)t.tv_nsec;
}
static void put64(unsigned char *p, uint64_t n) {
    for (unsigned i = 0; i != 8; ++i) p[7-i] = (unsigned char)(n >> (8*i));
}
static void all(const void *buffer, size_t length) {
    const unsigned char *p = buffer;
    while (length) {
        ssize_t n = write(output, p, length);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) _exit(92);
        p += n; length -= (size_t)n;
    }
}
static void record(uint64_t kind, uint64_t id, const void *buffer, size_t length) {
    if (output < 0 || length > 65552) _exit(93);
    unsigned char header[64] = "P115TLS2";
    pthread_mutex_lock(&records);
    put64(header+8, sequence++); put64(header+16, kind); put64(header+24, id);
    put64(header+32, stamp(CLOCK_MONOTONIC)); put64(header+40, stamp(CLOCK_REALTIME));
    put64(header+48, (uint64_t)getpid()); put64(header+56, length);
    all(header, sizeof(header)); all(buffer, length);
    if (fsync(output)) _exit(94);
    pthread_mutex_unlock(&records);
}
static void initialize(void) {
    const char *path = getenv("PHASE115_TLS_LOG");
    if (!path || path[0] != '/') _exit(95);
    output = open(path, O_WRONLY|O_CREAT|O_EXCL|O_CLOEXEC, 0600);
    if (output < 0) _exit(96);
    next_new = dlsym(RTLD_NEXT, "SSL_new");
    next_free = dlsym(RTLD_NEXT, "SSL_free");
    next_read = dlsym(RTLD_NEXT, "SSL_read_ex");
    next_write = dlsym(RTLD_NEXT, "SSL_write_ex");
    next_handshake = dlsym(RTLD_NEXT, "SSL_do_handshake");
    if (!next_new || !next_free || !next_read || !next_write || !next_handshake) _exit(97);
    record(0, 0, NULL, 0);
}
__attribute__((constructor)) static void loaded(void) { pthread_once(&once, initialize); }
__attribute__((destructor)) static void finished(void) {
    if (output >= 0) { record(6, 0, NULL, 0); close(output); output = -1; }
}
static uint64_t identity(SSL *ssl) {
    uint64_t id = 0;
    pthread_mutex_lock(&identities);
    for (unsigned i = 0; i != 64; ++i) if (connections[i].ssl == ssl) id = connections[i].id;
    pthread_mutex_unlock(&identities);
    if (!id) _exit(98);
    return id;
}
SSL *SSL_new(SSL_CTX *context) {
    pthread_once(&once, initialize);
    SSL *ssl = next_new(context);
    if (!ssl) return NULL;
    pthread_mutex_lock(&identities);
    unsigned i;
    for (i = 0; i != 64 && connections[i].ssl; ++i) {}
    if (i == 64 || next_id == UINT64_MAX) _exit(99);
    connections[i].ssl = ssl; connections[i].id = ++next_id;
    uint64_t id = connections[i].id;
    pthread_mutex_unlock(&identities);
    record(1, id, NULL, 0);
    return ssl;
}
void SSL_free(SSL *ssl) {
    pthread_once(&once, initialize);
    if (ssl) {
        uint64_t id = identity(ssl);
        record(5, id, NULL, 0);
        pthread_mutex_lock(&identities);
        for (unsigned i = 0; i != 64; ++i) if (connections[i].ssl == ssl) connections[i].ssl = NULL;
        pthread_mutex_unlock(&identities);
    }
    next_free(ssl);
}
int SSL_write_ex(SSL *ssl, const void *buffer, size_t size, size_t *written) {
    pthread_once(&once, initialize);
    uint64_t id = identity(ssl);
    record(7, id, buffer, size);
    int result = next_write(ssl, buffer, size, written);
    int saved_errno = errno;
    if (result == 1) record(3, id, buffer, *written);
    else record(8, id, NULL, 0);
    errno = saved_errno;
    return result;
}
int SSL_read_ex(SSL *ssl, void *buffer, size_t size, size_t *received) {
    pthread_once(&once, initialize);
    int result = next_read(ssl, buffer, size, received);
    if (result == 1) record(4, identity(ssl), buffer, *received);
    return result;
}
int SSL_do_handshake(SSL *ssl) {
    pthread_once(&once, initialize);
    int result = next_handshake(ssl);
    if (result == 1) {
        X509 *cert = SSL_get1_peer_certificate(ssl);
        unsigned char hash[32]; unsigned int size = 0;
        if (!cert || !X509_digest(cert, EVP_sha256(), hash, &size) || size != 32) _exit(100);
        X509_free(cert);
        record(2, identity(ssl), hash, size);
    }
    return result;
}
