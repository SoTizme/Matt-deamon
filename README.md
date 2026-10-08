# Matt_daemon

A small C++98 daemon for the mandatory 42 subject. Production code consists of
`main`, `MattDaemon` and `Tintin_reporter`; it uses POSIX calls, no threads and
no daemon framework.

## Code style and layout

The style reference is the files marked `By: shilal` in
[SoTizme/Webserv](https://github.com/SoTizme/Webserv/tree/6cbbabda5ac64ebee0a4042f0932b6a00db0d5e7),
especially `main.cpp`, `Client.hpp`, `Client.cpp` and `Request.cpp`.
Production files use 42 headers, plain member names, camelCase methods,
explicit constructors/destructors and colocated class headers. Like Webserv's
[Makefile](https://github.com/SoTizme/Webserv/blob/6cbbabda5ac64ebee0a4042f0932b6a00db0d5e7/Makefile),
the build uses `c++`, `-Wall -Wextra -Werror -std=c++98`, grouped file lists
and `addprefix`. Headers are listed directly as prerequisites to keep incremental builds correct.
C++98 is a project choice; subject version 4.3 permits any C++ version and
leaves file naming and layout free.

```text
main.cpp                       root check and entry point
daemon/MattDaemon.hpp          daemon class and resource ownership
daemon/MattDaemon.cpp          locking, signals, detachment and lifecycle
daemon/Server.cpp              TCP methods of the same daemon class
include/Tintin_reporter.hpp    reporter class
include/Tintin_reporter.cpp    logfile and timestamps
tests/                        mandatory integration checks
```

## Build and use

Build on Linux as your normal user:

```sh
make
./Matt_daemon                    # non-root execution fails on stderr
sudo ./Matt_daemon               # parent returns after successful startup
sudo ./Matt_daemon               # second instance fails on stderr
ss -ltn 'sport = :4242'
nc localhost 4242
```

In netcat, type messages and press Enter after each. Type `quit` and press Enter
to stop the daemon. Some Ubuntu netcat versions keep waiting for keyboard input
after the daemon closes the connection; press Enter again or Ctrl+D to exit
netcat. The first Enter after `quit` already stops the daemon; the second only
returns netcat to the shell. Check its log with:

```sh
sudo tail -20 /var/log/matt_daemon/matt_daemon.log
```

For a quick complete client session:

```sh
printf 'hello\ntest\nquit\n' | nc localhost 4242
```

For a signal shutdown after restarting:

```sh
sudo ./Matt_daemon
sudo kill -TERM "$(pgrep -x Matt_daemon)"
```

`make clean` removes object files. `make fclean` also removes the
executable. `make re` rebuilds from scratch. Header dependencies and parallel
builds are supported.

## Messages and clients

The daemon listens on IPv4 port 4242 on all interfaces. Three clients can be
active; an additional connection is accepted only to close it immediately.

TCP is a byte stream. Messages end at LF, CRLF or client EOF, so commands remain
correct when split across reads or when several arrive in one read. Only the
entire message `quit` stops the daemon: `quite`, ` quit`, `quit ` and `QUIT`
are ordinary logged text. To send a command without a newline, finish the stream
with `shutdown(SHUT_WR)` or close the connection. An unfinished message remains
buffered until its delimiter, disconnection or daemon shutdown.

Messages longer than 64 KiB are logged in chunks to bound memory; a trailing
`quit` inside such a message is ordinary text. Received partial input is also
preserved on EOF, reset and clean shutdown. Log entries use the subject example:

```text
[07/10/2026-12:34:56] [ INFO ] - Matt_daemon: Started.
[07/10/2026-12:34:57] [ LOG ] - Matt_daemon: User input: hello
```

## Tests

Run on Linux with root rights, with port 4242 and the daemon lock idle:

```sh
sudo ./tests/test_mandatory.sh
```

The suite prints PASS or FAIL for its checks and returns nonzero on failure.
It builds as the checkout owner, preserves existing system logs, stops only the
processes it starts, and leaves the daemon stopped. Python 3, make and a C/C++
compiler are required. `tests/faults.c` is a test-only LD_PRELOAD helper for
forcing error paths; it is never linked into the production executable.

See [AUDIT.md](AUDIT.md) for the requirement audit and verified results. The old
Phase 1-only test is available in the initial Git checkpoint.

## System calls and resource ownership

| Call | Purpose, success and failure |
| --- | --- |
| `geteuid` | Checks effective root rights before creating files; otherwise exits 1 on stderr. |
| `open`, `mkdir` | Reserve missing standard descriptors, open/create the lock, create the log directory and open /dev/null. Errors stop startup visibly. The reporter checks the directory and logfile stream separately. |
| `flock` | `LOCK_EX \| LOCK_NB` acquires the singleton lock immediately or fails visibly. Its descriptor remains open until the owner exits. |
| `socket`, `setsockopt`, `bind`, `listen` | Create the IPv4 listener, enable `SO_REUSEADDR`, bind port 4242 and listen. These run before detachment, so failures reach terminal stderr and release resources. |
| `pipe`, `fork` | Start a child sharing the open lock/log/listener. A startup pipe tells the parent whether detachment succeeded. The parent closes its copies and exits; failed children are waited for. |
| `setsid`, `chdir`, `dup2` | Detach the child into its own session, change cwd to / and replace standard streams with /dev/null. The child logs failures; the parent reports startup failure. |
| `sigaction`, `sigprocmask` | Install catchable-signal handlers and clear an inherited blocked mask. The handler only records the first signal in `volatile sig_atomic_t`. |
| `select` | Wait for listener/client activity; interrupted waits retry. A one-second timeout bounds the race where a signal arrives just before waiting, without busy-looping. Other errors trigger logged shutdown. |
| `accept`, `recv` | Accept a client into a free slot and read its bytes. Sockets are nonblocking; `EINTR` and `EAGAIN` retry. EOF/reset frees the slot; input errors are logged. |
| `close` | Clean shutdown closes all clients and the listener, then records quitting. The destructor closes any remaining descriptors and the lock. The logfile stream closes automatically. |

The lock file remains at `/var/lock/matt_daemon.lock` after shutdown. An existing
unlocked file is reusable. Keeping its inode avoids unlink/recreate races; file
existence alone does not mean the daemon is running. The parent never explicitly
unlocks the shared lock after forking, which would also unlock the child's lock.

The reporter appends and flushes every entry, uses local-time timestamps, and
reports failures formatting or writing a log. Resource-owning classes disable
copying through private, undefined copy constructors and assignment operators,
giving each resource one owner in C++98.

SIGKILL and SIGSTOP cannot be caught, blocked or handled. Linux/glibc's reserved
signal numbers are skipped when `sigaction` rejects them. Externally delivered
fault signals are logged like other catchable signals; a recurring synchronous
programming fault cannot be repaired by returning from a handler, so its handler
resets to the default action after its first invocation.

AI assistance was used for implementation and tests. The Git checkpoints retain
each phase for review; you should understand these calls before peer evaluation.
