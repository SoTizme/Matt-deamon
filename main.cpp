#include "daemon/MattDaemon.hpp"

#include <cerrno>
#include <cstring>
#include <exception>
#include <fcntl.h>
#include <stdexcept>
#include <string>
#include <iostream>
#include <unistd.h>

int main() {

	if (geteuid() != 0) {
		std::cerr << "Matt_daemon: root privileges required." << std::endl;
		return 1;
	}
	try {
		// Fill missing 0, 1 or 2 with /dev/null so important files do not use them.
		for (int fd = 0; fd < 3; ++fd) {
			if (fcntl(fd, F_GETFD) != -1)
				continue;

			if (errno != EBADF) {
				throw std::runtime_error( std::string("Can't inspect stdio: ") + std::strerror(errno));
			}

			const int nullFd = ::open("/dev/null", O_RDWR);
			if (nullFd == -1) {
				throw std::runtime_error(std::string("Can't prepare stdio: ") + std::strerror(errno));
			}
		}
		MattDaemon daemon;
		daemon.run();
	}
	catch (const std::exception &error) {
		std::cerr << "Matt_daemon: " << error.what() << std::endl;
		return 1;
	}
	return 0;
}
