#include "MattDaemon.hpp"

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sstream>
#include <sys/file.h>
#include <sys/wait.h>
#include <unistd.h>

volatile sig_atomic_t MattDaemon::receivedSignal = 0;

MattDaemon::MattDaemon() : lockFd(-1), reporter(), server(reporter) {}

MattDaemon::~MattDaemon() {
	server.closeAll();
	if (lockFd != -1)
		close(lockFd);
}

void MattDaemon::recordSignal(int number) {
	// Only save the signal here. Normal code will do the cleanup.
	if (receivedSignal == 0)
		receivedSignal = number;
}

void MattDaemon::installSignals() {
	struct sigaction action = {};
	action.sa_handler = recordSignal;
	if (sigfillset(&action.sa_mask) == -1)
		throw std::runtime_error("Failed to initialize signal mask.");
	for (int number = 1; number < NSIG; ++number)
	{
		if (number == SIGKILL || number == SIGSTOP)
			continue;
		// If a fatal signal happens again, let the system stop the process.
		action.sa_flags = (number == SIGSEGV || number == SIGBUS || number == SIGFPE || number == SIGILL || number == SIGTRAP) ? SA_RESETHAND : 0;
		if (sigaction(number, &action, NULL) == -1 && errno != EINVAL)
		{
			throw std::runtime_error(std::string("sigaction: ") + std::strerror(errno));
		}
	}
	// Clear signals that were blocked before this program started.
	sigset_t unblocked;
	if (sigemptyset(&unblocked) == -1 || sigprocmask(SIG_SETMASK, &unblocked, NULL) == -1)
	{
		throw std::runtime_error(std::string("sigprocmask: ") + std::strerror(errno));
	}
}

std::runtime_error MattDaemon::systemError(const std::string &action) {
	return std::runtime_error(action + ": " + std::strerror(errno));
}

void MattDaemon::acquireLock()  {
	lockFd = ::open(LOCK_PATH, O_CREAT | O_RDWR | O_CLOEXEC, 0644);

	if (lockFd == -1)
	{
		throw systemError(std::string("Can't open :") + LOCK_PATH);
	}
	// Keep this file open so another daemon cannot take the lock.
	if (flock(lockFd, LOCK_EX | LOCK_NB) == -1)
	{
		throw systemError(std::string("Can't open :") + LOCK_PATH + " (exclusive lock)");
	}
}

bool MattDaemon::daemonize() {
	// The child sends one byte here when its setup is finished.
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
		// Parent waits for '1'. No byte means the child failed.
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
		// Create a new session and leave the terminal.
		if (setsid() == -1)
			throw systemError("setsid");
		// Move to / so the old folder is not kept busy.
		if (chdir("/") == -1)
			throw systemError("chdir");
		// Send stdin, stdout and stderr to /dev/null.
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
		// Send '1' to tell the parent that startup worked.
		ssize_t result;
		do {
			result = write(startup[1], "1", 1);
		} while (result == -1 && errno == EINTR);

		if (result != 1)
			throw systemError("startup notification");
	}
	catch (const std::exception &error) {
		try {
			reporter.error(error.what());
		}
		catch (...) { }
		close(startup[1]);
		throw;
	}
	close(startup[1]);
	return true;
}

void MattDaemon::run()
{
	acquireLock();
	reporter.open();
	try
	{
		reporter.info("Started.");
		server.create();
		reporter.info("Entering Daemon mode.");
		if (!daemonize())
			return;
		server.serve(receivedSignal);
		if (receivedSignal != 0)
		{
			std::stringstream ss;
			ss << receivedSignal;
			reporter.info("Signal received: " + ss.str() + " (" + strsignal(receivedSignal) + ").");
		}
		server.shutdown();
		reporter.info("Quitting.");
	}
	catch (const std::exception &error)
	{
		try { reporter.error(error.what()); }
		catch (...) { }
		throw;
	}
}
