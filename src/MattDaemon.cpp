#include "MattDaemon.hpp"

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <stdexcept>
#include <string>
#include <sys/file.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

namespace {
const char* const LOCK_PATH = "/var/lock/matt_daemon.lock";
volatile sig_atomic_t receivedSignal = 0;

void recordSignal(int number) {
    if (receivedSignal == 0)
        receivedSignal = number;
}

void installSignals() {
    struct sigaction action = {};
    action.sa_handler = recordSignal;
    if (sigfillset(&action.sa_mask) == -1)
        throw std::runtime_error("Failed to initialize signal mask.");
    for (int number = 1; number < NSIG; ++number) {
        if (number == SIGKILL || number == SIGSTOP)
            continue;
        // A recurring synchronous fault cannot be recovered by returning.
        action.sa_flags = (number == SIGSEGV || number == SIGBUS
                           || number == SIGFPE || number == SIGILL
                           || number == SIGTRAP) ? SA_RESETHAND : 0;
        if (sigaction(number, &action, NULL) == -1 && errno != EINVAL)
            throw std::runtime_error(std::string("sigaction: ")
                                     + std::strerror(errno));
    }
    // Do not inherit a caller's blocked termination signals.
    sigset_t unblocked;
    if (sigemptyset(&unblocked) == -1
        || sigprocmask(SIG_SETMASK, &unblocked, NULL) == -1)
        throw std::runtime_error(std::string("sigprocmask: ")
                                 + std::strerror(errno));
}

std::runtime_error systemError(const std::string& action) {
    return std::runtime_error(action + ": " + std::strerror(errno));
}
}

MattDaemon::MattDaemon() : _lockFd(-1), _serverFd(-1) {
    for (int index = 0; index < MAX_CLIENTS; ++index) {
        _clients[index] = -1;
        _continued[index] = false;
    }
}

MattDaemon::~MattDaemon() {
    for (int index = 0; index < MAX_CLIENTS; ++index) {
        if (_clients[index] != -1)
            close(_clients[index]);
    }
    if (_serverFd != -1)
        close(_serverFd);
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
        installSignals();
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

void MattDaemon::createServer() {
    _reporter.info("Creating server.");
    _serverFd = socket(AF_INET, SOCK_STREAM, 0);
    if (_serverFd == -1)
        throw systemError("socket");
    if (_serverFd >= FD_SETSIZE)
        throw std::runtime_error("Server descriptor exceeds select capacity.");
    const int reuse = 1;
    if (setsockopt(_serverFd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) == -1)
        throw systemError("setsockopt");
    if (fcntl(_serverFd, F_SETFL, O_NONBLOCK) == -1)
        throw systemError("fcntl server");
    struct sockaddr_in address = {};
    address.sin_family = AF_INET;
    address.sin_port = htons(4242);
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    if (bind(_serverFd, reinterpret_cast<struct sockaddr*>(&address),
             sizeof(address)) == -1)
        throw systemError("bind port 4242");
    if (listen(_serverFd, MAX_CLIENTS) == -1)
        throw systemError("listen");
    _reporter.info("Server created.");
}

void MattDaemon::acceptClient() {
    const int client = accept(_serverFd, NULL, NULL);
    if (client == -1) {
        if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK
            || errno == ECONNABORTED)
            return;
        throw systemError("accept");
    }
    int index = 0;
    while (index < MAX_CLIENTS && _clients[index] != -1)
        ++index;
    if (index == MAX_CLIENTS || client >= FD_SETSIZE) {
        close(client);
        _reporter.info("Client rejected: capacity reached.");
        return;
    }
    _clients[index] = client;
    if (fcntl(client, F_SETFL, O_NONBLOCK) == -1)
        throw systemError("fcntl client");
    _pending[index].clear();
    _continued[index] = false;
    _reporter.info("Client connected.");
}

void MattDaemon::closeClient(int index) {
    close(_clients[index]);
    _clients[index] = -1;
    _pending[index].clear();
    _continued[index] = false;
    _reporter.info("Client disconnected.");
}

bool MattDaemon::finishMessage(int index, bool newline) {
    std::string& message = _pending[index];
    if (newline && !message.empty() && message[message.size() - 1] == '\r')
        message.erase(message.size() - 1);
    if (!_continued[index] && message == "quit") {
        message.clear();
        _reporter.info("Request quit.");
        return false;
    }
    _reporter.input(message);
    message.clear();
    _continued[index] = false;
    return true;
}

bool MattDaemon::readClient(int index) {
    char buffer[4096];
    const ssize_t count = recv(_clients[index], buffer, sizeof(buffer), 0);
    if (count == -1) {
        if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)
            return true;
        _reporter.error(std::string("recv: ") + std::strerror(errno));
        const bool keepRunning = _pending[index].empty()
            || finishMessage(index, false);
        closeClient(index);
        return keepRunning;
    }
    if (count == 0) {
        const bool keepRunning = _pending[index].empty()
            || finishMessage(index, false);
        closeClient(index);
        return keepRunning;
    }
    for (ssize_t pos = 0; pos < count; ++pos) {
        if (buffer[pos] == '\n') {
            if (!finishMessage(index, true))
                return false;
        } else {
            // Bound memory while preserving arbitrarily long input in chunks.
            if (_pending[index].size() == 65536) {
                _reporter.input(_pending[index]);
                _pending[index].clear();
                _continued[index] = true;
            }
            _pending[index] += buffer[pos];
        }
    }
    return true;
}

void MattDaemon::serve() {
    while (receivedSignal == 0) {
        fd_set readable;
        FD_ZERO(&readable);
        FD_SET(_serverFd, &readable);
        int highest = _serverFd;
        for (int index = 0; index < MAX_CLIENTS; ++index) {
            if (_clients[index] != -1) {
                FD_SET(_clients[index], &readable);
                if (_clients[index] > highest)
                    highest = _clients[index];
            }
        }
        // Recreated each iteration: select may modify its timeout.
        // A bounded wait covers a signal arriving just before select starts.
        struct timeval timeout = {1, 0};
        const int result = select(highest + 1, &readable, NULL, NULL, &timeout);
        if (result == -1) {
            if (errno == EINTR)
                continue;
            throw systemError("select");
        }
        if (receivedSignal != 0)
            break;
        // Service existing clients before admitting a queued connection.
        for (int index = 0; index < MAX_CLIENTS; ++index) {
            if (_clients[index] != -1 && FD_ISSET(_clients[index], &readable)
                && !readClient(index))
                return;
        }
        if (FD_ISSET(_serverFd, &readable))
            acceptClient();
    }
}

void MattDaemon::run() {
    acquireLock();
    _reporter.open();
    try {
        _reporter.info("Started.");
        createServer();
        _reporter.info("Entering Daemon mode.");
        if (!daemonize())
            return;
        serve();
        if (receivedSignal != 0)
            _reporter.info("Signal received: " + std::to_string(receivedSignal)
                           + " (" + strsignal(receivedSignal) + ").");
        for (int index = 0; index < MAX_CLIENTS; ++index) {
            if (_clients[index] != -1) {
                if (!_pending[index].empty())
                    finishMessage(index, false);
                closeClient(index);
            }
        }
        close(_serverFd);
        _serverFd = -1;
        _reporter.info("Server closed.");
        _reporter.info("Quitting.");
    } catch (const std::exception& error) {
        try { _reporter.error(error.what()); } catch (...) {}
        throw;
    }
}
