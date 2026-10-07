#include "Tintin_reporter.hpp"

#include <cerrno>
#include <cstring>
#include <ctime>
#include <stdexcept>
#include <sys/stat.h>

namespace {
const char* const LOG_DIRECTORY = "/var/log/matt_daemon";
const char* const LOG_PATH = "/var/log/matt_daemon/matt_daemon.log";
}

void Tintin_reporter::open() {
    if (mkdir(LOG_DIRECTORY, 0755) == -1 && errno != EEXIST)
        throw std::runtime_error(std::string("Can't create :") + LOG_DIRECTORY
                                 + ": " + std::strerror(errno));
    struct stat directory;
    if (stat(LOG_DIRECTORY, &directory) == -1)
        throw std::runtime_error(std::string("Can't inspect :") + LOG_DIRECTORY
                                 + ": " + std::strerror(errno));
    if (!S_ISDIR(directory.st_mode))
        throw std::runtime_error(std::string(LOG_DIRECTORY) + " is not a directory.");
    _stream.open(LOG_PATH, std::ios::out | std::ios::app);
    if (!_stream.is_open())
        throw std::runtime_error(std::string("Can't open :") + LOG_PATH);
}

void Tintin_reporter::info(const std::string& message) {
    write("INFO", message);
}

void Tintin_reporter::input(const std::string& message) {
    write("LOG", "User input: " + message);
}

void Tintin_reporter::error(const std::string& message) {
    write("ERROR", message);
}

void Tintin_reporter::write(const std::string& level, const std::string& message) {
    const std::time_t now = std::time(NULL);
    struct tm localTime;
    char timestamp[32];
    if (now == static_cast<std::time_t>(-1)
        || localtime_r(&now, &localTime) == NULL
        || std::strftime(timestamp, sizeof(timestamp), "[%d/%m/%Y-%H:%M:%S]",
                         &localTime) == 0)
        throw std::runtime_error("Failed to format log timestamp.");
    _stream << timestamp << " [ " << level << " ] - Matt_daemon: "
            << message << std::endl;
    if (!_stream)
        throw std::runtime_error(std::string("Can't write :") + LOG_PATH);
}
