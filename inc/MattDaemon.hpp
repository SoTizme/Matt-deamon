#ifndef MATT_DAEMON_HPP
#define MATT_DAEMON_HPP

#include "Tintin_reporter.hpp"

class MattDaemon {
public:
    MattDaemon();
    ~MattDaemon();
    MattDaemon(const MattDaemon&) = delete;
    MattDaemon& operator=(const MattDaemon&) = delete;

    void run();

private:
    int _lockFd;
    Tintin_reporter _reporter;
    void acquireLock();
    bool daemonize();
};

#endif
