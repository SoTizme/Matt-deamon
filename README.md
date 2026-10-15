# Matt_daemon

## About

Matt_daemon is a C++98 school project that introduces Unix daemons, process
management, TCP sockets, file locking, logging, and signal handling.

The program runs in the background and listens for messages on TCP port `4242`.
Every received message is written to a log file. The exact message `quit` stops
the daemon.

## Features

- Runs as a background daemon
- Accepts connections on TCP port `4242`
- Supports up to three clients at the same time
- Writes timestamped messages to a log file
- Uses an exclusive lock to prevent multiple instances
- Stops cleanly after receiving `quit` or a catchable signal
- Uses POSIX system calls without the `daemon()` function

## Project structure

```text
.
├── main.cpp                       Program entry point
├── Makefile                       Compilation rules
├── daemon/
│   ├── MattDaemon.hpp             Daemon class declaration
│   ├── MattDaemon.cpp             Lifecycle, locking and signals
│   └── Server.cpp                 TCP server and client handling
└── include/
    ├── Tintin_reporter.hpp        Logger class declaration
    └── Tintin_reporter.cpp        Log formatting and file output
```

## Requirements

The project must be run on Linux. You need:

- A C++ compiler
- `make`
- Root privileges
- `nc` (netcat) to connect to the server
- `ss` or `netstat` to inspect the listening port

## Compilation

Compile the project with:

```sh
make
```

This creates the executable:

```text
Matt_daemon
```

Available Makefile rules:

```sh
make          # Build the executable
make clean    # Remove object files
make fclean   # Remove object files and the executable
make re       # Rebuild the project from scratch
```

## Running the daemon

The daemon needs root privileges because it creates files under `/var/log` and
`/var/lock`.

```sh
sudo ./Matt_daemon
```

After successful startup, the parent process exits and the daemon continues in
the background.

Check that it is running:

```sh
pgrep -a Matt_daemon
sudo ss -ltnp | grep 4242
```

Only one instance can run at a time. Starting the program again while it is
already running returns an error because the lock is already held.

## Connecting to the server

Open a client connection with netcat:

```sh
nc localhost 4242
```

Type a message and press Enter:

```text
hello
this is a test
```

Messages from connected clients are added to the log file. A maximum of three
clients can be active at the same time.

## Stopping the daemon

Send the exact message below from a connected client:

```text
quit
```

The comparison is case-sensitive. Messages such as `QUIT`, `quit `, or `quite`
are logged as normal messages and do not stop the daemon.

The daemon can also be stopped with a signal:

```sh
sudo kill -TERM "$(pgrep -x Matt_daemon)"
```

`SIGKILL` and `SIGSTOP` cannot be caught or handled by any program.

## Log and lock files

Messages and daemon events are written to:

```text
/var/log/matt_daemon/matt_daemon.log
```

Read the latest entries with:

```sh
sudo tail -20 /var/log/matt_daemon/matt_daemon.log
```

Example:

```text
[07/10/2026-12:34:56] [ INFO ] - Matt_daemon: Started.
[07/10/2026-12:35:03] [ LOG ] - Matt_daemon: User input: hello
[07/10/2026-12:35:10] [ INFO ] - Matt_daemon: Request quit.
[07/10/2026-12:35:10] [ INFO ] - Matt_daemon: Quitting.
```

The instance lock is stored at:

```text
/var/lock/matt_daemon.lock
```

The lock file may remain after shutdown. The program uses the active kernel lock,
not the existence of the file, to determine whether another instance is running.

## Program flow

```text
Check root privileges
        |
Acquire the exclusive lock
        |
Open the log and create the TCP server
        |
Fork and detach from the terminal
        |
Wait for clients with select()
        |
Log messages until quit or a signal is received
        |
Close sockets and release resources
```

The server uses `select()` so one process can manage the listening socket and up
to three client sockets without threads.
