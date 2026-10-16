#include "Tintin_reporter.hpp"

#include <cerrno>
#include <cstring>
#include <ctime>
#include <stdexcept>
#include <sys/stat.h>

Tintin_reporter::Tintin_reporter() {}

Tintin_reporter::~Tintin_reporter() {
	if (outfile.is_open())
		outfile.close();
}

void Tintin_reporter::open() {
	// Create the log folder, or continue if it already exists.
	if (mkdir(LOG_DIRECTORY, 0755) == -1 && errno != EEXIST)
		throw std::runtime_error(std::string("Can't create :") + LOG_DIRECTORY
								 + ": " + std::strerror(errno));
	struct stat directory;
	if (stat(LOG_DIRECTORY, &directory) == -1)
		throw std::runtime_error(std::string("Can't inspect :") + LOG_DIRECTORY
								 + ": " + std::strerror(errno));
	if (!S_ISDIR(directory.st_mode))
		throw std::runtime_error(std::string(LOG_DIRECTORY) + " is not a directory.");

	outfile.open(LOG_PATH, std::ios::out | std::ios::app);
	if (!outfile.is_open())
		throw std::runtime_error(std::string("Can't open :") + LOG_PATH);
}

std::string Tintin_reporter::clean(const std::string &message) {
	std::string result(message);

	// Do not let control characters create fake lines in the log.
	for (std::string::size_type index = 0; index < result.size(); ++index) {
		const unsigned char character = static_cast<unsigned char>(result[index]);
		if (character < 32 || character == 127)
			result[index] = '?';
	}
	return result;
}

void Tintin_reporter::write(const std::string & level, const std::string & message) {
	const std::time_t now = std::time(NULL);
	if (now == static_cast<std::time_t>(-1))
		throw std::runtime_error("Failed to get current time.");

	struct tm localTime;
	if (localtime_r(&now, &localTime) == NULL)
		throw std::runtime_error("Failed to convert current time.");

	char timestamp[32];
	if (std::strftime(timestamp, sizeof(timestamp), "[%d/%m/%Y-%H:%M:%S]", &localTime) == 0)
		throw std::runtime_error("Failed to format log timestamp.");

	outfile << timestamp << " [ " << level << " ] - Matt_daemon: "
			<< clean(message) << std::endl;
    if (!outfile)
		throw std::runtime_error(std::string("Can't write :") + LOG_PATH);
}

void Tintin_reporter::info(const std::string & message) {
	write("INFO", message);
}

void Tintin_reporter::input(const std::string & message) {
	write("LOG", "User input: " + message);
}

void Tintin_reporter::error(const std::string & message) {
	write("ERROR", message);
}
