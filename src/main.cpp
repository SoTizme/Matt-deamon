#include "MattDaemon.hpp"

#include <exception>
#include <iostream>
#include <unistd.h>

int main() {
    if (geteuid() != 0) {
        std::cerr << "Matt_daemon: root privileges required." << std::endl;
        return 1;
    }
    try {
        MattDaemon daemon;
        daemon.run();
    } catch (const std::exception& error) {
        std::cerr << "Matt_daemon: " << error.what() << std::endl;
        return 1;
    }
    return 0;
}
