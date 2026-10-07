#include "MattDaemon.hpp"

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <stdexcept>
#include <string>
#include <sys/file.h>
#include <sys/wait.h>
#include <unistd.h>

namespace {
const char* const LOCK_PATH = "/var/lock/matt_daemon.lock";

std::runtime_error systemError(const std::string& action) {
    return std::runtime_error(action + ": " + std::strerror(errno));
}
}

MattDaemon::MattDaemon() : _lockFd(-1) {}

MattDaemon::~MattDaemon() {
    if (_lockFd != -1)
        close(_lockFd);
}

void MattDaemon::acquireLock() {
    _lockFd = ::open(LOCK_PATH, O_CREAT | O_RDWR | O_CLOEXEC, 0644);
    if (_lockFd == -1)
        throw systemError(std::string("Can't open :") + LOCK_PATH);
    if (flock(_lockFd, LOCK_EX | LOCK_NB) == -1)
        throw systemError(std::string("Can't open :") + LOCK_PATH
                          + " (exclusive lock)");
}

bool MattDaemon::daemonize() {
    int startup[2];
    if (pipe(startup) == -1)
        throw systemError("pipe");
    const pid_t child = fork();
    if (child == -1) {
        const int savedError = errno;
        close(startup[0]);
        close(startup[1]);
        errno = savedError;
        throw systemError("fork");
    }
    if (child > 0) {
        close(startup[1]);
        char ready = 0;
        ssize_t result;
        do {
            result = read(startup[0], &ready, 1);
        } while (result == -1 && errno == EINTR);
        close(startup[0]);
        if (result != 1 || ready != '1') {
            while (waitpid(child, NULL, 0) == -1 && errno == EINTR) {}
            throw std::runtime_error("Daemon startup failed; check the log.");
        }
        return false;
    }

    close(startup[0]);
    try {
        if (setsid() == -1)
            throw systemError("setsid");
        if (chdir("/") == -1)
            throw systemError("chdir");
        const int nullFd = ::open("/dev/null", O_RDWR);
        if (nullFd == -1)
            throw systemError("Can't open /dev/null");
        for (int fd = 0; fd < 3; ++fd) {
            if (dup2(nullFd, fd) == -1) {
                const int savedError = errno;
                close(nullFd);
                errno = savedError;
                throw systemError("dup2");
            }
        }
        if (nullFd > 2)
            close(nullFd);
        _reporter.info("started. PID: " + std::to_string(getpid()) + ".");
        ssize_t result;
        do {
            result = write(startup[1], "1", 1);
        } while (result == -1 && errno == EINTR);
        if (result != 1)
            throw systemError("startup notification");
    } catch (const std::exception& error) {
        try { _reporter.error(error.what()); } catch (...) {}
        close(startup[1]);
        throw;
    }
    close(startup[1]);
    return true;
}

void MattDaemon::run() {
    acquireLock();
    _reporter.open();
    _reporter.info("Started.");
    _reporter.info("Entering Daemon mode.");
    if (!daemonize())
        return;
    // Phase 2 wait; the TCP event loop replaces this in Phase 3.
    for (;;)
        pause();
}
