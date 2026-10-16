#ifndef SERVER_HPP
#define SERVER_HPP

#include "../include/Tintin_reporter.hpp"

#include <signal.h>
#include <stdexcept>
#include <string>

class Server {
	private:
		static const int PORT = 4242;
		static const int MAX_CLIENTS = 3;

		Tintin_reporter &reporter;
		int serverFd;
		int clients[MAX_CLIENTS];
		// Keep unfinished text here until a newline arrives.
		std::string pending[MAX_CLIENTS];
		// Remember when a long line was already logged in parts.
		bool continued[MAX_CLIENTS];

		Server(const Server &other);
		Server &operator=(const Server &other);

		std::runtime_error systemError(const std::string &action) const;
		void setNonBlocking(int fd, const std::string &name);
		void acceptClient();
		bool readClient(int index);
		bool finishMessage(int index, bool newline);
		void closeClient(int index);

	public:
		explicit Server(Tintin_reporter &reporter);
		~Server();

		void create();
		void serve(const volatile sig_atomic_t &receivedSignal);
		void shutdown();
		void closeAll();
};

#endif
