The main lesson is **how to build a Linux service that runs independently of your terminal, receives work, and shuts down cleanly**.

For your defense, you should be able to explain the program’s lifecycle, justify its system calls, and demonstrate its behavior. The subject explicitly checks `fork`, `chdir`, exclusive `flock`, and signal handling. It also asks you to understand your code and be transparent about AI assistance. :codex-file-citation{path="/home/shilal/Desktop/matt/subject/en.subject.pdf" purpose="source"}

A good opening explanation is:

> “Matt_daemon is a background TCP logging service. It accepts up to three clients on port 4242, saves their messages with timestamps, prevents multiple instances, and shuts down when it receives `quit` or a catchable signal.”

**Start by understanding this flow:**

```text
Check root rights
        ↓
Acquire lock → open logfile → create listening socket
        ↓
       fork
      /    \
 Parent    Child
 waits     detaches from terminal
 returns   waits for connections/messages
                    ↓
             logs received messages
                    ↓
               quit or signal
                    ↓
             closes resources and exits
```

These are the concepts you should learn, in this order:

1. **Processes and `fork`**

   A process is a running program. `fork()` creates a child process.

   Its return value tells the two processes which branch to execute:

   - `-1`: creation failed.
   - `0`: this is the child.
   - Positive value: this is the parent; the value is the child’s PID.

   Your parent returns to the shell. The child continues running as the daemon.

2. **How the child becomes a daemon**

   Know what each operation accomplishes:

   | Operation | Purpose in your implementation |
   |---|---|
   | `setsid()` | Creates a new session, initially without a controlling terminal. |
   | `chdir("/")` | Gives the daemon a working directory independent of where it was launched. |
   | `dup2()` with `/dev/null` | Redirects standard input, output, and error away from the terminal. |
   | Startup `pipe()` | Lets the child report successful startup before the parent returns. |

   This explains why successful startup prints nothing. Later activity goes into the logfile.

3. **File descriptors and resource ownership**

   A file descriptor is an integer identifying an open file, socket, or pipe.

   Standard descriptors are:

   ```text
   0 = stdin
   1 = stdout
   2 = stderr
   ```

   Your program also owns the lock descriptor, listening socket, and client sockets.

   You should be able to point to where each resource is opened and closed. After `fork`, the child inherits the parent’s open descriptors.

4. **Why the lock is necessary**

   Your program opens:

   ```text
   /var/lock/matt_daemon.lock
   ```

   Then it requests:

   ```cpp
   flock(lockFd, LOCK_EX | LOCK_NB)
   ```

   `LOCK_EX` means exclusive ownership. `LOCK_NB` makes a competing instance fail immediately.

   **The existence of the file does not mean the daemon is running.** The kernel-held lock determines that.

   The child keeps the descriptor open throughout its lifetime. The parent closes its copy; the child’s inherited copy keeps the lock alive. Closing the final copy releases the lock.

5. **Networking and TCP messages**

   You already used networking in Webserv. Be ready to explain this sequence:

   ```text
   socket → bind → listen → accept → recv
   ```

   The listening socket accepts connections. Each accepted client gets a separate socket.

   **TCP carries a stream of bytes.** One `recv()` can contain half a message, one message, or several messages.

   Your code therefore keeps a buffer per client and recognizes messages at newline or EOF. This lets it recognize `quit` even when it arrives as separate pieces such as `qu` and `it\n`.

   `quite`, `QUIT`, and `quit ` are ordinary messages because the command comparison is exact.

6. **How one process handles three clients**

   `select()` waits until the listener or a client socket becomes readable. This lets one process handle multiple clients without threads or constantly consuming CPU.

   The code maintains three client slots. A fourth connection is accepted and immediately closed.

   Understand these `recv()` results:

   | Result | Meaning |
   |---|---|
   | Greater than zero | Bytes were received. |
   | Zero | The peer finished sending. |
   | `-1` | Check `errno` to determine the error. |

   `EINTR` means a signal interrupted an operation. `EAGAIN` means a nonblocking operation currently has no data available.

7. **Signals and clean shutdown**

   A signal is an operating-system notification sent to a process. For example:

   ```sh
   sudo kill -TERM "$(pgrep -x Matt_daemon)"
   ```

   Your `sigaction()` handler records the signal in `volatile sig_atomic_t`. Normal program execution then logs it and performs cleanup. Stream operations can be unsafe inside a signal handler, which is why logging happens afterward.

   **SIGKILL and SIGSTOP cannot be caught.** Be ready to explain this exception.

   Also remember your netcat observation: the first Enter after `quit` stops the daemon; the second Enter closes the waiting netcat client.

8. **Logging and C++ ownership**

   `Tintin_reporter` creates the log directory, appends timestamped entries, and flushes each entry.

   Your destructors release owned resources. Copying these classes is disabled through private copy constructors and assignment operators, preventing two objects from owning and closing the same resources.

   C++98 is your chosen project style. The subject permits any C++ version.

**Then understand the smaller choices in your code:** why missing standard descriptors are reserved before opening resources, why the listener is created before detachment, why `select` has a one-second timeout, and why long messages are logged in bounded chunks.

Read the code in this order:

- [main.cpp](/home/shilal/Desktop/matt/main.cpp:24): entry point and root check.
- [MattDaemon::run](/home/shilal/Desktop/matt/daemon/MattDaemon.cpp:154): overall lifecycle, then follow the methods it calls.
- [Server.cpp](/home/shilal/Desktop/matt/daemon/Server.cpp:23): connections and messages.
- [Tintin_reporter.cpp](/home/shilal/Desktop/matt/include/Tintin_reporter.cpp:33): logging.

For each system call, practice explaining **what it does, how failure is detected, and how resources are cleaned up afterward**.

During evaluation, demonstrate non-root rejection, background execution, second-instance rejection, three clients plus a rejected fourth, message logging, `quit`, signal shutdown, and successful restart. Check the logfile, process list, and listening port to prove the results. The subject requests a Linux VM with kernel version greater than 3.14.

- Explain startup
- Explain fork
- Practice defense