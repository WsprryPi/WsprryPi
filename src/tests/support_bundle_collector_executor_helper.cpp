#include <csignal>
#include <filesystem>
#include <fstream>
#include <string>
#include <unistd.h>

int main(int argc, char *argv[]) {
    if (argc < 3) {
        return 64;
    }
    if (std::string(argv[1]) != "--output-dir") {
        return 64;
    }

    const std::filesystem::path job_directory(argv[2]);
    std::ofstream arguments(job_directory / "executor-helper-argv.txt");
    for (int index = 0; index < argc; ++index) {
        arguments << argv[index] << '\n';
    }
    arguments.close();

    std::ofstream(job_directory / "executor-helper-pid.txt") << getpid() << '\n';
    std::ofstream(job_directory / "executor-helper-started.txt") << "started\n";

    const std::string mode = job_directory.filename().string();
    if (mode == "nonzero") {
        return 23;
    }
    if (mode == "timeout") {
        std::signal(SIGTERM, SIG_IGN);
        for (;;) {
            pause();
        }
    }
    if (mode == "blocked") {
        for (;;) {
            pause();
        }
    }
    return 0;
}
