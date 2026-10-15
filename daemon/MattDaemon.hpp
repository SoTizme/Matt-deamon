#ifndef MATT_DAEMON_HPP
#define MATT_DAEMON_HPP

#include "../include/Tintin_reporter.hpp"
#include <signal.h>
#include <stdexcept>
#include <string>

class MattDaemon {
	private:
		static const int MAX_CLIENTS = 3;
		static volatile sig_atomic_t receivedSignal;

		int lockFd;
		int serverFd;
		int clients[MAX_CLIENTS];
		std::string pending[MAX_CLIENTS];
		bool continued[MAX_CLIENTS];
		Tintin_reporter reporter;

		// A descriptor owner must not be copied.
		MattDaemon(const MattDaemon &other);
		MattDaemon &operator=(const MattDaemon &other);

		static void recordSignal(int number);
		void installSignals();
		std::runtime_error systemError(const std::string &action);
		void acquireLock();
		bool daemonize();

		void createServer();
		void serve();
		void acceptClient();
		bool readClient(int index);
		bool finishMessage(int index, bool newline);
		void closeClient(int index);

	public:
		MattDaemon();
		~MattDaemon();

		void run();
};

#endif
