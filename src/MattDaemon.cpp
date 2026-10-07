#include "MattDaemon.hpp"

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <stdexcept>
#include <string>
#include <sys/file.h>
#include <unistd.h>

namespace {
const char* const LOCK_PATH = "/var/lock/matt_daemon.lock";
}

MattDaemon::MattDaemon() : _lockFd(-1) {}

MattDaemon::~MattDaemon() {
    if (_lockFd != -1)
        close(_lockFd);
}

void MattDaemon::acquireLock() {
    _lockFd = ::open(LOCK_PATH, O_CREAT | O_RDWR | O_CLOEXEC, 0644);
    if (_lockFd == -1)
        throw std::runtime_error(std::string("Can't open :") + LOCK_PATH
                                 + ": " + std::strerror(errno));
    if (flock(_lockFd, LOCK_EX | LOCK_NB) == -1) {
        const int savedError = errno;
        throw std::runtime_error(std::string("Can't open :") + LOCK_PATH
                                 + " (exclusive lock): "
                                 + std::strerror(savedError));
    }
}

void MattDaemon::run() {
    acquireLock();
    _reporter.open();
    _reporter.info("Started.");
    std::cout << "Phase 1: lock acquired. Press Enter to stop." << std::endl;
    // Temporary foreground wait; daemonization replaces this in Phase 2.
    std::cin.get();
    if (std::cin.bad())
        throw std::runtime_error("Failed to read standard input.");
    _reporter.info("Quitting.");
}
