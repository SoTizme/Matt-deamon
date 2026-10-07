#ifndef MATT_DAEMON_HPP
#define MATT_DAEMON_HPP

#include "Tintin_reporter.hpp"
#include <string>

class MattDaemon {
public:
    MattDaemon();
    ~MattDaemon();
    MattDaemon(const MattDaemon&) = delete;
    MattDaemon& operator=(const MattDaemon&) = delete;

    void run();

private:
    static const int MAX_CLIENTS = 3;
    int _lockFd;
    int _serverFd;
    int _clients[MAX_CLIENTS];
    std::string _pending[MAX_CLIENTS];
    bool _continued[MAX_CLIENTS];
    Tintin_reporter _reporter;

    void acquireLock();
    bool daemonize();
    void createServer();
    void serve();
    void acceptClient();
    bool readClient(int index);
    bool finishMessage(int index, bool newline);
    void closeClient(int index);
};

#endif
