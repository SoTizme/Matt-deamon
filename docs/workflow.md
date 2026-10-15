The easiest way to defend this project is to understand it as one lifecycle, then explain each function’s role within that lifecycle.

First, fix this before evaluation: [main.cpp](/home/shilal/Desktop/Matt-deamon/main.cpp:13) currently prints the UID and has the mandatory root check commented out. It should be:

```cpp
if (geteuid() != 0) {
	std::cerr << "Matt_daemon: root privileges required." << std::endl;
	return 1;
}
```

Remove this debug line:

```cpp
std::cout << geteuid() << std::endl;
```

## 20-second project presentation

You can begin your defense with:

> Matt_daemon is a background TCP server written in C++98. It runs with root privileges, listens on port 4242, accepts up to three clients, and writes their messages into a timestamped log file. An exclusive file lock prevents multiple instances. The daemon stops cleanly when it receives the exact message `quit` or a catchable signal.

## Complete workflow

```text
main()
  |
  |-- Check root privileges
  |-- Ensure stdin, stdout and stderr exist
  |-- Construct MattDaemon
  |
MattDaemon::run()
  |
  |-- Acquire exclusive lock
  |-- Open log file
  |-- Create TCP listening socket
  |-- fork()
       |
       |-- Parent waits for startup confirmation and exits
       |
       `-- Child:
            |-- Install signal handlers
            |-- Create a new session with setsid()
            |-- Change directory to /
            |-- Redirect standard descriptors to /dev/null
            |-- Enter select() server loop
            |-- Accept and read clients
            |-- Stop on "quit" or signal
            `-- Close sockets and release resources
```

## 1. Program entry point

See [main.cpp](/home/shilal/Desktop/Matt-deamon/main.cpp:1).

### Lines 1–10: includes

These headers provide:

- `MattDaemon.hpp`: your daemon class
- `cerrno`: the `errno` error number
- `cstring`: `strerror()` to translate `errno` into text
- `fcntl.h`: `fcntl()` and `open()`
- `stdexcept`: exceptions such as `runtime_error`
- `iostream`: `std::cerr`
- `unistd.h`: `geteuid()` and POSIX functions

### Lines 12–17: root check

```cpp
if (geteuid() != 0)
```

`geteuid()` returns the effective user ID:

- Root has UID `0`.
- Any other value means the process is not running as root.

The program must reject non-root users before creating the lock, log, or socket.

### Lines 21–25: reserve descriptors 0, 1 and 2

Unix processes normally have:

```text
0 = stdin
1 = stdout
2 = stderr
```

`fcntl(fd, F_GETFD)` checks whether each descriptor exists. If one is closed, the code opens `/dev/null` to occupy that number.

Why? Without this protection, the lock file might accidentally become descriptor `0`. Later, `dup2()` could replace descriptor `0` and unintentionally close the lock.

### Lines 26–27: create and run the daemon

```cpp
MattDaemon daemon;
daemon.run();
```

The constructor initializes all descriptors. `run()` performs the actual startup and server lifecycle.

### Lines 28–30: top-level error handling

Every `runtime_error` eventually reaches this block. The error is printed to `stderr`, and the program returns failure status `1`.

## 2. The MattDaemon class

See [MattDaemon.hpp](/home/shilal/Desktop/Matt-deamon/daemon/MattDaemon.hpp:9).

### Important members

```cpp
static const int MAX_CLIENTS = 3;
```

The server can keep exactly three active clients.

```cpp
static volatile sig_atomic_t receivedSignal;
```

The signal handler writes the received signal number here.

- `sig_atomic_t` can safely be written from a signal handler.
- `volatile` tells the compiler that the value can change unexpectedly.
- It is `static` because a C-style signal handler cannot depend on a specific object through `this`.

```cpp
int lockFd;
int serverFd;
int clients[MAX_CLIENTS];
```

These store the lock, listener, and client file descriptors.

```cpp
std::string pending[MAX_CLIENTS];
```

Each client needs its own input buffer because TCP is a byte stream.

```cpp
bool continued[MAX_CLIENTS];
```

This records whether a very long message was already written in chunks. It prevents the last part of a long message from accidentally being interpreted as the standalone `quit` command.

```cpp
Tintin_reporter reporter;
```

This object owns and writes the log file.

### Lines 21–23: copying is disabled

The copy constructor and assignment operator are private and not implemented.

This prevents code such as:

```cpp
MattDaemon second = first;
```

Copying would be dangerous because both objects would believe they owned the same descriptors and might close them twice.

This is the C++98 equivalent of disabling copying for a resource-owning class.

## 3. Main lifecycle: `run()`

Start your code explanation with [MattDaemon::run()](/home/shilal/Desktop/Matt-deamon/daemon/MattDaemon.cpp:145), even though it appears near the bottom of the file.

```cpp
acquireLock();
reporter.open();
```

The program first guarantees that it is the only instance, then opens the log.

```cpp
reporter.info("Started.");
createServer();
reporter.info("Entering Daemon mode.");
```

It logs startup and creates the listening socket before detaching. Therefore, errors such as “port already occupied” can still reach the original terminal.

```cpp
if (!daemonize())
	return;
```

After `fork()`:

- The parent gets `false`, returns, and exits.
- The child gets `true` and continues.

```cpp
serve();
```

The child enters the TCP event loop.

After `serve()` returns, the program:

1. Logs a received signal if there was one.
2. Flushes partial client messages.
3. Closes every connected client.
4. Closes the server socket.
5. Logs shutdown.

The destructor later closes the lock descriptor, releasing the exclusive lock.

## 4. Exclusive instance lock

See [acquireLock()](/home/shilal/Desktop/Matt-deamon/daemon/MattDaemon.cpp:67).

```cpp
lockFd = open(LOCK_PATH, O_CREAT | O_RDWR | O_CLOEXEC, 0644);
```

This opens or creates:

```text
/var/lock/matt_daemon.lock
```

`0644` means:

```text
owner: read + write
group: read
others: read
```

Then:

```cpp
flock(lockFd, LOCK_EX | LOCK_NB)
```

- `LOCK_EX`: request an exclusive lock.
- `LOCK_NB`: do not wait if another instance owns it.

If a second daemon starts, `flock()` fails immediately.

Important defense answer:

> The existence of the lock file does not prove that the daemon is running. The active kernel lock is what matters. The descriptor must remain open for the entire daemon lifetime.

## 5. Logger

See [Tintin_reporter.cpp](/home/shilal/Desktop/Matt-deamon/include/Tintin_reporter.cpp:21).

### `open()`

```cpp
mkdir(LOG_DIRECTORY, 0755)
```

Creates `/var/log/matt_daemon`.

`EEXIST` is accepted because the directory may already exist. The subsequent `stat()` confirms that the path really is a directory.

```cpp
outfile.open(LOG_PATH, std::ios::out | std::ios::app);
```

`app` means new records are appended instead of overwriting older logs.

### `write()`

```cpp
std::time(NULL)
```

Gets the current Unix time.

```cpp
localtime_r(...)
```

Converts it into local calendar fields. The `_r` version writes into the provided structure.

```cpp
strftime(..., "[%d/%m/%Y-%H:%M:%S]", ...)
```

Formats the timestamp.

```cpp
outfile << ... << std::endl;
```

Writes and flushes the log entry. `std::endl` adds a newline and flushes the stream.

## 6. TCP server creation

See [createServer()](/home/shilal/Desktop/Matt-deamon/daemon/Server.cpp:11).

```cpp
socket(AF_INET, SOCK_STREAM, 0)
```

Creates:

- `AF_INET`: IPv4 socket
- `SOCK_STREAM`: TCP socket
- `0`: use the normal TCP protocol

```cpp
setsockopt(..., SO_REUSEADDR, ...)
```

Allows the server to reuse port 4242 shortly after a previous instance stops.

```cpp
fcntl(serverFd, F_SETFL, O_NONBLOCK)
```

Makes the listener nonblocking. An operation will return instead of freezing the entire daemon.

The address is configured with:

```cpp
address.sin_family = AF_INET;
address.sin_port = htons(4242);
address.sin_addr.s_addr = htonl(INADDR_ANY);
```

- `htons(4242)` converts the port into network byte order.
- `INADDR_ANY` listens on every local IPv4 interface.

Then:

```cpp
bind(...)
listen(serverFd, MAX_CLIENTS)
```

- `bind()` attaches the socket to port 4242.
- `listen()` turns it into a listening socket.

## 7. Daemonization

See [daemonize()](/home/shilal/Desktop/Matt-deamon/daemon/MattDaemon.cpp:79).

### Startup pipe

```cpp
int startup[2];
pipe(startup);
```

A pipe has two ends:

```text
startup[0] = read end
startup[1] = write end
```

The child writes `"1"` after successful daemonization. The parent waits to receive it.

### `fork()`

```cpp
const pid_t child = fork();
```

Possible results:

- `-1`: failure
- `0`: current process is the child
- Greater than `0`: current process is the parent, and the value is the child PID

### Parent branch

The parent closes the write end and waits for one byte:

```cpp
read(startup[0], &ready, 1);
```

If it receives `"1"`, startup succeeded, so it returns from `daemonize()` and exits.

If the child closes the pipe without sending `"1"`, startup failed. The parent waits for the child and reports an error.

### Child branch

```cpp
installSignals();
setsid();
chdir("/");
```

- `installSignals()` prepares signal handling.
- `setsid()` creates a new session and detaches from the controlling terminal.
- `chdir("/")` prevents the daemon from keeping the launch directory busy.

The child opens `/dev/null` and runs:

```cpp
dup2(nullFd, 0);
dup2(nullFd, 1);
dup2(nullFd, 2);
```

This redirects stdin, stdout, and stderr away from the terminal.

Finally, the child logs its PID and sends `"1"` to the waiting parent.

## 8. Handling multiple clients with `select()`

See [serve()](/home/shilal/Desktop/Matt-deamon/daemon/Server.cpp:117).

Each loop creates a descriptor set:

```cpp
fd_set readable;
FD_ZERO(&readable);
FD_SET(serverFd, &readable);
```

Then every active client is added with `FD_SET()`.

```cpp
select(highest + 1, &readable, NULL, NULL, &timeout);
```

`select()` waits for one of these events:

- A new client is waiting on the server socket.
- An existing client sent data.
- A client disconnected.
- A signal interrupted the wait.
- The one-second timeout expired.

`highest + 1` tells `select()` how many descriptor numbers to examine.

After `select()`:

```cpp
FD_ISSET(fd, &readable)
```

checks which descriptors are ready.

Existing clients are processed before accepting another connection. This allows a disconnected slot to become free before a waiting connection is considered.

## 9. Accepting clients

See [acceptClient()](/home/shilal/Desktop/Matt-deamon/daemon/Server.cpp:35).

```cpp
const int client = accept(serverFd, NULL, NULL);
```

`accept()` creates a new socket specifically for that client. The original `serverFd` continues listening for other clients.

The code searches for a free slot:

```cpp
while (index < MAX_CLIENTS && clients[index] != -1)
	++index;
```

If all three slots are occupied, the new client is immediately closed.

Otherwise:

1. Store the client descriptor.
2. Make it nonblocking.
3. Clear its pending buffer.
4. Log the connection.

## 10. Reading TCP messages

See [readClient()](/home/shilal/Desktop/Matt-deamon/daemon/Server.cpp:82).

```cpp
recv(clients[index], buffer, sizeof(buffer), 0);
```

Possible results:

- Positive: number of bytes received
- `0`: client closed the connection
- `-1`: an error occurred

TCP does not preserve messages. For example, sending:

```text
hello\n
```

could arrive as:

```text
recv 1: "he"
recv 2: "llo\n"
```

That is why each client has a persistent `pending[index]` string.

The code examines every received byte:

```cpp
if (buffer[pos] == '\n')
	finishMessage(...);
else
	pending[index] += buffer[pos];
```

### Example: fragmented `quit`

First `recv()`:

```text
qu
```

The pending buffer becomes `"qu"`.

Second `recv()`:

```text
it\n
```

The pending buffer becomes `"quit"`. The newline calls `finishMessage()`, which recognizes the exact command and stops the server.

### Example: several messages in one `recv()`

```text
hello\nworld\n
```

The loop finds both newline characters and logs two separate messages.

## 11. Recognizing `quit`

See [finishMessage()](/home/shilal/Desktop/Matt-deamon/daemon/Server.cpp:67).

First, it removes `\r` from Windows/netcat-style `\r\n` endings.

Then:

```cpp
if (!continued[index] && message == "quit")
```

Only an exact complete message stops the daemon.

These do not stop it:

```text
QUIT
quit 
 quite
```

They are logged like normal input.

Returning `false` propagates through:

```text
finishMessage() → readClient() → serve() → run()
```

That causes orderly shutdown.

## 12. Signal handling

See [recordSignal()](/home/shilal/Desktop/Matt-deamon/daemon/MattDaemon.cpp:17).

The handler only does this:

```cpp
receivedSignal = number;
```

That is intentional. Logging, allocating strings, or closing complex C++ streams inside a signal handler is unsafe.

`select()` may return `-1` with `errno == EINTR` when interrupted by a signal. The loop checks the stored signal and exits normally.

`SIGKILL` and `SIGSTOP` are skipped because the operating system does not allow programs to catch them.

Example:

```sh
sudo kill -TERM "$(pgrep -x Matt_daemon)"
```

Workflow:

```text
Kernel sends SIGTERM
        |
recordSignal(15)
        |
receivedSignal becomes 15
        |
select() returns or timeout expires
        |
serve() exits
        |
run() logs the signal
        |
clients and server are closed
        |
destructor closes the lock
```

## 13. Resource cleanup

See [MattDaemon::~MattDaemon()](/home/shilal/Desktop/Matt-deamon/daemon/MattDaemon.cpp:59).

The destructor closes:

- Every remaining client descriptor
- The server descriptor
- The lock descriptor

Closing the last copy of `lockFd` releases the kernel lock.

`Tintin_reporter` separately closes its `ofstream` in its destructor.

## Concrete full example

Suppose you run:

```sh
sudo ./Matt_daemon
```

Conceptually, descriptors may look like:

```text
0 stdin
1 stdout
2 stderr
3 lock file
4 log file
5 server socket
6 startup pipe read end
7 startup pipe write end
```

After `fork()`, both processes initially have copies. The parent closes what it does not need and exits. The child redirects descriptors `0–2`, closes the startup pipe, and keeps the lock, log, and server descriptors.

Then a client runs:

```sh
nc localhost 4242
```

The server accepts it, possibly as descriptor `6`, and stores it in `clients[0]`.

The client sends:

```text
hello
```

The daemon receives `hello\n`, recognizes the newline, and writes:

```text
[date-time] [ LOG ] - Matt_daemon: User input: hello
```

The client sends:

```text
quit
```

`finishMessage()` returns `false`, the server loop ends, sockets close, and the lock is released.

## Questions you should practice

1. Why use `flock()` instead of checking whether the lock file exists?
2. Why must `lockFd` remain open?
3. What are the three possible `fork()` return cases?
4. Why does the parent wait on a pipe?
5. What does `setsid()` do?
6. Why call `chdir("/")`?
7. Why redirect descriptors 0, 1, and 2?
8. What is the difference between the listening socket and a client socket?
9. Why can one `recv()` contain partial or multiple messages?
10. Why use one pending buffer per client?
11. What does `select()` return?
12. Why is the signal handler minimal?
13. Why can SIGKILL and SIGSTOP not be handled?
14. Why are copy construction and assignment disabled?
15. Where is every file descriptor closed?

For each system call, learn four answers:

> What does it do? What indicates success? What indicates failure? Who closes the resulting resource?