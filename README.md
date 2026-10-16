# Matt_daemon

A small **Linux daemon and TCP server** developed as part of the **42 Matt_daemon project**.

---

## Project Overview

**Matt_daemon** is an educational C++98 project that runs silently in the background and listens for TCP connections on port **4242**.

The goal of the project is to learn how Unix services work, including:

- Creating a background process
- Managing TCP clients
- Writing timestamped logs
- Handling Unix signals
- Preventing multiple instances
- Cleaning up system resources correctly

The daemon accepts up to three clients at the same time. Every received message is written to a log file, and the exact message `quit` stops the service.

---

## Key Features

- Runs as a background daemon
- Listens on TCP port `4242`
- Accepts up to three clients at once
- Logs client messages and daemon events
- Uses a lock file to allow only one running instance
- Handles catchable signals and shuts down cleanly
- Supports messages split across several TCP reads
- Uses non-blocking sockets with `select()`
- Written without using the `daemon()` helper function

---

## Technical Choices

| Category | Choice |
| --- | --- |
| Language | C++98 |
| Platform | Linux |
| Network | TCP/IPv4 |
| Port | 4242 |
| Client limit | 3 |
| I/O handling | Non-blocking sockets and `select()` |
| Logging | Custom `Tintin_reporter` class |
| Instance control | `flock()` |
| Process mode | Unix daemon |

---

## Repository Structure

~~~txt
.
├── main.cpp
├── Makefile
│
├── daemon/
│   ├── MattDaemon.hpp
│   ├── MattDaemon.cpp
│   ├── Server.hpp
│   └── Server.cpp
│
└── include/
    ├── Tintin_reporter.hpp
    └── Tintin_reporter.cpp
~~~

- `MattDaemon` manages the lock, signals, and daemon startup.
- `Server` manages connections and client messages.
- `Tintin_reporter` creates and writes the log file.

---

## Requirements

You need:

- A Linux system
- A C++ compiler
- `make`
- Root privileges
- `nc` (netcat) for testing
- `ss` or `netstat` to inspect the port

---

## How to Use the Project

### 1. Build the program

~~~sh
make
~~~

This creates the executable:

~~~txt
Matt_daemon
~~~

### 2. Start the daemon

The program needs root privileges because it creates files under `/var/log` and `/var/lock`.

~~~sh
sudo ./Matt_daemon
~~~

When startup succeeds, the terminal returns and the daemon continues running in the background.

### 3. Check that it is running

~~~sh
pgrep -a Matt_daemon
sudo ss -ltnp | grep 4242
~~~

### 4. Connect to the server

Open another terminal and run:

~~~sh
nc localhost 4242
~~~

Write a message and press Enter:

~~~txt
hello
this is my first test
~~~

The messages will be saved in the log.

### 5. Read the log

~~~sh
sudo tail -f /var/log/matt_daemon/matt_daemon.log
~~~

Example output:

~~~txt
[16/10/2026-14:20:10] [ INFO ] - Matt_daemon: Started.
[16/10/2026-14:20:10] [ INFO ] - Matt_daemon: Server created.
[16/10/2026-14:20:15] [ INFO ] - Matt_daemon: Client connected.
[16/10/2026-14:20:20] [ LOG ] - Matt_daemon: User input: hello
~~~

Press `Ctrl+C` to stop following the log. This does not stop the daemon.

### 6. Stop the daemon

From a connected netcat client, send the exact message:

~~~txt
quit
~~~

The word is case-sensitive. For example, `QUIT`, `quit `, and `quite` are normal messages and do not stop the daemon.

You can also stop it with a signal:

~~~sh
sudo kill -TERM "$(pgrep -x Matt_daemon)"
~~~

---

## Testing Examples

### Test the single-instance lock

Start the daemon, then try to start it again:

~~~sh
sudo ./Matt_daemon
~~~

The second instance should fail because the first instance owns the lock.

### Test several clients

Open three terminals and run this command in each one:

~~~sh
nc localhost 4242
~~~

All three clients can send messages. A fourth connection is rejected until one client disconnects.

### Test signal handling

~~~sh
sudo kill -INT "$(pgrep -x Matt_daemon)"
~~~

The daemon records the signal in the log and closes its sockets.

---

## Log and Lock Files

| Purpose | Path |
| --- | --- |
| Log file | `/var/log/matt_daemon/matt_daemon.log` |
| Lock file | `/var/lock/matt_daemon.lock` |

The lock file can remain after the daemon stops. This is normal: the program checks the active kernel lock, not only whether the file exists.

---

## Makefile Commands

~~~sh
make          # Build the project
make clean    # Remove object files
make fclean   # Remove object files and the executable
make re       # Rebuild everything
~~~

---

## Program Flow

~~~txt
Check root privileges
        |
Acquire the lock
        |
Open the log and create the server
        |
Fork and detach from the terminal
        |
Wait for clients with select()
        |
Log messages until "quit" or a signal
        |
Close sockets and release resources
~~~

---

## Learning Objectives

This project demonstrates:

- Unix process management
- Daemon creation with `fork()` and `setsid()`
- TCP socket programming
- Non-blocking I/O
- Multiplexing with `select()`
- Safe signal handling
- File locking with `flock()`
- Resource cleanup with C++ classes

---

## Notes

- This project is intended for Linux.
- It must be started with root privileges.
- Only the exact message `quit` stops the daemon through a client.
- `SIGKILL` and `SIGSTOP` cannot be caught by any program.
- This is an educational project, not a production network service.

---

Developed as part of the **42 Matt_daemon Project**.
