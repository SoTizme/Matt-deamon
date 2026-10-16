#ifndef MATT_DAEMON_HPP
#define MATT_DAEMON_HPP

#include "Server.hpp"
#include "../include/Tintin_reporter.hpp"
#include <signal.h>
#include <stdexcept>
#include <string>

#define LOCK_PATH "/var/lock/matt_daemon.lock"

class MattDaemon {
	private:
		// Save the first signal so run() can stop the daemon safely.
		static volatile sig_atomic_t receivedSignal;

		int lockFd;
		Tintin_reporter reporter;
		Server server;

		// A descriptor owner must not be copied.
		MattDaemon(const MattDaemon &other);
		MattDaemon &operator=(const MattDaemon &other);

		static void recordSignal(int number);
		void installSignals();
		std::runtime_error systemError(const std::string &action);
		void acquireLock();
		bool daemonize();

	public:
		MattDaemon();
		~MattDaemon();

		void run();
};

#endif
