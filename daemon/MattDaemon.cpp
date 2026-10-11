#include "MattDaemon.hpp"

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sstream>
#include <sys/file.h>
#include <sys/wait.h>
#include <unistd.h>

namespace {
const char *const LOCK_PATH = "/var/lock/matt_daemon.lock";
}

volatile sig_atomic_t MattDaemon::receivedSignal = 0;

void MattDaemon::recordSignal(int number) {
	if (receivedSignal == 0)
		receivedSignal = number;
}

void MattDaemon::installSignals() {
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

std::runtime_error MattDaemon::systemError(const std::string &action) {
	return std::runtime_error(action + ": " + std::strerror(errno));
}

MattDaemon::MattDaemon() : lockFd(-1), serverFd(-1) {
	for (int index = 0; index < MAX_CLIENTS; ++index) {
		clients[index] = -1;
		continued[index] = false;
	}
}

MattDaemon::~MattDaemon() {
	for (int index = 0; index < MAX_CLIENTS; ++index) {
		if (clients[index] != -1)
			close(clients[index]);
	}
	if (serverFd != -1)
		close(serverFd);
	if (lockFd != -1)
		close(lockFd);
}

void MattDaemon::acquireLock() {
	lockFd = ::open(LOCK_PATH, O_CREAT | O_RDWR | O_CLOEXEC, 0644);

	if (lockFd == -1)
		throw systemError(std::string("Can't open :") + LOCK_PATH);
	if (flock(lockFd, LOCK_EX | LOCK_NB) == -1)
		throw systemError(std::string("Can't open :") + LOCK_PATH  + " (exclusive lock)");
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
		std::stringstream ss;
		ss << getpid();
		reporter.info("started. PID: " + ss.str() + ".");
		ssize_t result;
		do {
			result = write(startup[1], "1", 1);
		} while (result == -1 && errno == EINTR);
		if (result != 1)
			throw systemError("startup notification");
	} catch (const std::exception & error) {
		try { reporter.error(error.what()); } catch (...) {}
		close(startup[1]);
		throw;
	}
	close(startup[1]);
	return true;
}

void MattDaemon::run() {
	acquireLock();
	reporter.open();
	try {
		reporter.info("Started.");
		createServer();
		reporter.info("Entering Daemon mode.");
		if (!daemonize())
			return;
		serve();
		if (receivedSignal != 0) {
			std::stringstream ss;
			ss << receivedSignal;
			reporter.info("Signal received: " + ss.str()
						   + " (" + strsignal(receivedSignal) + ").");
		}
		for (int index = 0; index < MAX_CLIENTS; ++index) {
			if (clients[index] != -1) {
				if (!pending[index].empty())
					finishMessage(index, false);
				closeClient(index);
			}
		}
		close(serverFd);
		serverFd = -1;
		reporter.info("Server closed.");
		reporter.info("Quitting.");
	} catch (const std::exception & error) {
		try { reporter.error(error.what()); } catch (...) {}
		throw;
	}
}
