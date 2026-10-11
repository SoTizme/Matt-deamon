#include "MattDaemon.hpp"

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

void MattDaemon::createServer() {
	reporter.info("Creating server.");
	serverFd = socket(AF_INET, SOCK_STREAM, 0);
	if (serverFd == -1)
		throw systemError("socket");
	if (serverFd >= FD_SETSIZE)
		throw std::runtime_error("Server descriptor exceeds select capacity.");
	const int reuse = 1;
	if (setsockopt(serverFd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) == -1)
		throw systemError("setsockopt");
	if (fcntl(serverFd, F_SETFL, O_NONBLOCK) == -1)
		throw systemError("fcntl server");
	struct sockaddr_in address = {};
	address.sin_family = AF_INET;
	address.sin_port = htons(4242);
	address.sin_addr.s_addr = htonl(INADDR_ANY);
	if (bind(serverFd, reinterpret_cast<struct sockaddr*>(&address),
			 sizeof(address)) == -1)
		throw systemError("bind port 4242");
	if (listen(serverFd, MAX_CLIENTS) == -1)
		throw systemError("listen");
	reporter.info("Server created.");
}

void MattDaemon::acceptClient() {
	const int client = accept(serverFd, NULL, NULL);
	if (client == -1) {
		if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK
			|| errno == ECONNABORTED)
			return;
		throw systemError("accept");
	}
	int index = 0;
	while (index < MAX_CLIENTS && clients[index] != -1)
		++index;
	if (index == MAX_CLIENTS || client >= FD_SETSIZE) {
		close(client);
		reporter.info("Client rejected: capacity reached.");
		return;
	}
	clients[index] = client;
	if (fcntl(client, F_SETFL, O_NONBLOCK) == -1)
		throw systemError("fcntl client");
	pending[index].clear();
	continued[index] = false;
	reporter.info("Client connected.");
}

void MattDaemon::closeClient(int index) {
	close(clients[index]);
	clients[index] = -1;
	pending[index].clear();
	continued[index] = false;
	reporter.info("Client disconnected.");
}

bool MattDaemon::finishMessage(int index, bool newline) {
	std::string & message = pending[index];
	if (newline && !message.empty() && message[message.size() - 1] == '\r')
		message.erase(message.size() - 1);
	if (!continued[index] && message == "quit") {
		message.clear();
		reporter.info("Request quit.");
		return false;
	}
	reporter.input(message);
	message.clear();
	continued[index] = false;
	return true;
}

bool MattDaemon::readClient(int index) {
	char buffer[4096];
	const ssize_t count = recv(clients[index], buffer, sizeof(buffer), 0);
	if (count == -1) {
		if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)
			return true;
		reporter.error(std::string("recv: ") + std::strerror(errno));
		const bool keepRunning = pending[index].empty()
			|| finishMessage(index, false);
		closeClient(index);
		return keepRunning;
	}
	if (count == 0) {
		const bool keepRunning = pending[index].empty()
			|| finishMessage(index, false);
		closeClient(index);
		return keepRunning;
	}
	for (ssize_t pos = 0; pos < count; ++pos) {
		if (buffer[pos] == '\n') {
			if (!finishMessage(index, true))
				return false;
		} else {
			// Bound memory while preserving arbitrarily long input in chunks.
			if (pending[index].size() == 65536) {
				reporter.input(pending[index]);
				pending[index].clear();
				continued[index] = true;
			}
			pending[index] += buffer[pos];
		}
	}
	return true;
}

void MattDaemon::serve() {
	while (receivedSignal == 0) {
		fd_set readable;
		FD_ZERO(&readable);
		FD_SET(serverFd, &readable);
		int highest = serverFd;
		for (int index = 0; index < MAX_CLIENTS; ++index) {
			if (clients[index] != -1) {
				FD_SET(clients[index], &readable);
				if (clients[index] > highest)
					highest = clients[index];
			}
		}
		// Recreated each iteration: select may modify its timeout.
		// A bounded wait covers a signal arriving just before select starts.
		struct timeval timeout = {1, 0};
		const int result = select(highest + 1, &readable, NULL, NULL, &timeout);
		if (result == -1) {
			if (errno == EINTR)
				continue;
			throw systemError("select");
		}
		if (receivedSignal != 0)
			break;
		// Service existing clients before admitting a queued connection.
		for (int index = 0; index < MAX_CLIENTS; ++index) {
			if (clients[index] != -1 && FD_ISSET(clients[index], &readable)
				&& !readClient(index))
				return;
		}
		if (FD_ISSET(serverFd, &readable))
			acceptClient();
	}
}
