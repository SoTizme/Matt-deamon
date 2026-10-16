#include "Server.hpp"

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>

Server::Server(Tintin_reporter &log) : reporter(log), serverFd(-1) {
	for (int index = 0; index < MAX_CLIENTS; ++index) {
		clients[index] = -1;
		continued[index] = false;
	}
}

Server::~Server() {
	closeAll();
}

std::runtime_error Server::systemError(const std::string &action) const {
	return std::runtime_error(action + ": " + std::strerror(errno));
}

void Server::setNonBlocking(int fd, const std::string &name) {
	// Read the old flags, then add O_NONBLOCK without removing them.
	int flags;
	do {
		flags = fcntl(fd, F_GETFL);
	} while (flags == -1 && errno == EINTR);
	if (flags == -1)
		throw systemError("fcntl " + name + " F_GETFL");

	int result;
	do {
		result = fcntl(fd, F_SETFL, flags | O_NONBLOCK);
	} while (result == -1 && errno == EINTR);
	if (result == -1)
		throw systemError("fcntl " + name + " F_SETFL");
}

void Server::create() {
	reporter.info("Creating server.");
	serverFd = socket(AF_INET, SOCK_STREAM, 0);
	if (serverFd == -1)
		throw systemError("socket");
	// select() cannot watch a descriptor above this limit.
	if (serverFd >= FD_SETSIZE)
		throw std::runtime_error("Server descriptor exceeds select capacity.");

	// Allow the same port to be used again after a restart.
	const int reuse = 1;
	if (setsockopt(serverFd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) == -1)
		throw systemError("setsockopt");

	setNonBlocking(serverFd, "server");
	struct sockaddr_in address = {};
	address.sin_family = AF_INET;
	address.sin_port = htons(PORT);
	address.sin_addr.s_addr = htonl(INADDR_ANY);
	if (bind(serverFd, reinterpret_cast<struct sockaddr *>(&address),
			 sizeof(address)) == -1)
		throw systemError("bind port 4242");
	if (listen(serverFd, MAX_CLIENTS) == -1)
		throw systemError("listen");
	reporter.info("Server created.");
}

void Server::acceptClient() {
	const int client = accept(serverFd, NULL, NULL);
	if (client == -1) {
		if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK || errno == ECONNABORTED)
			return;
		throw systemError("accept");
	}
	int index = 0;
	while (index < MAX_CLIENTS && clients[index] != -1){
		++index;
	}
	// Reject this connection when all three client slots are full.
	if (index == MAX_CLIENTS || client >= FD_SETSIZE){
		close(client);
		reporter.info("Client rejected: capacity reached.");
		return;
	}
	clients[index] = client;
	setNonBlocking(client, "client");
	pending[index].clear();
	continued[index] = false;
	reporter.info("Client connected.");
}

void Server::closeClient(int index){
	close(clients[index]);
	clients[index] = -1;
	pending[index].clear();
	continued[index] = false;
	reporter.info("Client disconnected.");
}

bool Server::finishMessage(int index, bool newline){
	std::string &message = pending[index];

	if (newline && !message.empty() && message[message.size() - 1] == '\r')
		message.erase(message.size() - 1);
	// Stop only when the full message is exactly "quit".
	if (!continued[index] && message == "quit"){
		message.clear();
		reporter.info("Request quit.");
		return false;
	}
	reporter.input(message);
	message.clear();
	continued[index] = false;
	return true;
}

bool Server::readClient(int index){
	char buffer[4096];
	const ssize_t count = recv(clients[index], buffer, sizeof(buffer), 0);
	if (count == -1){
		if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)
			return true;
		reporter.error(std::string("recv: ") + std::strerror(errno));
		const bool keepRunning = pending[index].empty() || finishMessage(index, false);
		closeClient(index);
		return keepRunning;
	}
	// recv() returns 0 when the client disconnects.
	if (count == 0){
		const bool keepRunning = pending[index].empty() || finishMessage(index, false);
		closeClient(index);
		return keepRunning;
	}
	for (ssize_t pos = 0; pos < count; ++pos){
		if (buffer[pos] == '\n'){
			if (!finishMessage(index, true))
				return false;
		}
		else{
			// Write very long input in parts to limit memory use.
			if (pending[index].size() == 65536){
				reporter.input(pending[index]);
				pending[index].clear();
				continued[index] = true;
			}
			pending[index] += buffer[pos];
		}
	}
	return true;
}

void Server::serve(const volatile sig_atomic_t &receivedSignal){
	while (receivedSignal == 0)
	{
		fd_set readable;
		FD_ZERO(&readable);
		FD_SET(serverFd, &readable);
		int highest = serverFd;
		for (int index = 0; index < MAX_CLIENTS; ++index){
			if (clients[index] != -1){
				FD_SET(clients[index], &readable);
				if (clients[index] > highest) highest = clients[index];
			}
		}
		// select() changes timeout, so reset it before each call.
		struct timeval timeout = {1, 0};
		const int result = select(highest + 1, &readable, NULL, NULL, &timeout);
		if (result == -1){
			if (errno == EINTR)
				continue;
			throw systemError("select");
		}
		if (receivedSignal != 0)
			break;
		// Read clients first so a disconnected slot can be reused.
		for (int index = 0; index < MAX_CLIENTS; ++index) {
			if (clients[index] != -1 && FD_ISSET(clients[index], &readable) && !readClient(index))
				return;
		}
		if (FD_ISSET(serverFd, &readable))
			acceptClient();
	}
}

// Save unfinished messages and log a normal shutdown.
void Server::shutdown(){
	for (int index = 0; index < MAX_CLIENTS; ++index) {
		if (clients[index] != -1){
			if (!pending[index].empty())
				finishMessage(index, false);
			closeClient(index);
		}
	}
	if (serverFd != -1) {
		::close(serverFd);
		serverFd = -1;
		reporter.info("Server closed.");
	}
}

// Close anything still open without writing to the log.
void Server::closeAll(){
	for (int index = 0; index < MAX_CLIENTS; ++index){
		if (clients[index] != -1){
			::close(clients[index]);
			clients[index] = -1;
		}
	}
	if (serverFd != -1) {
		::close(serverFd);
		serverFd = -1;
	}
}
