# Mandatory audit

The mandatory implementation is complete. No bonus features were added.
The final Linux integration suite passed **104 checks** on Linux kernel
5.15.133.1-microsoft-standard-WSL2. A separate real netcat session logged hello
and test, quit stopped the daemon, and restart/SIGTERM left no process, held lock
or occupied port. The system reaped those daemon launches without the test
subreaper.

## Style migration

The current C++98 layout and coding conventions follow the Webserv files with
`By: shilal` headers at commit `6cbbabda5ac64ebee0a4042f0932b6a00db0d5e7`.
`main.cpp` is at the root, daemon methods are split by purpose in `daemon/`,
and the reporter implementation and header are together in `include/`.
The Makefile follows Webserv's grouped source lists and C++98 flags, with
explicit header prerequisites. The migration passed the full 104-check suite
and strict C++98 compilation. The mandatory behavior and two-class design
remain the same.

## Severity review

- **FAIL, fixed:** If the caller closed stdin, the lock could occupy descriptor 0
  and be replaced during daemon redirection. Reserve missing standard descriptors
  before acquiring the lock. Closed-stdio startup now retains the lock.
- **RISK, fixed:** A reset connection discarded text already received into a
  partial-message buffer. Flush it before releasing the client slot.
- **RISK, fixed:** A caller's blocked signal mask could prevent SIGTERM shutdown.
  Clear that mask in the child after installing handlers.
- **RISK, fixed:** A quit message remained buffered and was logged twice during
  cleanup. Clear it as soon as the command is recognized.
- **NICE TO HAVE:** Bonus features remain outside the requested mandatory scope.
  No architecture rewrite was needed.

## Requirement evidence

| Requirement | Evidence |
| --- | --- |
| C++ executable `Matt_daemon`; Makefile all/clean/fclean/re | Warning-as-error C++98 build, also with `-pedantic-errors`; all targets, parallel make, no-op incremental make and header-triggered rebuild pass. |
| Root only | Real UID 0 with non-root effective UID is rejected on stderr; no log/lock changes. Running child has effective UID 0. |
| Explicit fork; no `daemon()` | Source audit; detached live child is its own session/group leader, has no controlling terminal, cwd / and stdio /dev/null. |
| Startup errors visible | Listener created before fork. Startup pipe propagates child detachment failures to the parent; forced fork/setsid/chdir/dup2 failures produce nonzero startup status. |
| `Tintin_reporter`; correct log path and timestamps | Existing log prefix preserved; appended records match `[DD/MM/YYYY-HH:MM:SS] [ LEVEL ] - Matt_daemon: ...`. |
| Exclusive flock; correct lock path; second-instance stderr | Four competing launches rejected; daemon retains the actual lock descriptor after parent exit. Closed-stdio regression passes. |
| Stale lock and restart | Restart after quit, signals, startup failures and SIGKILL succeeds using the same unlocked file. |
| IPv4 TCP port 4242; SO_REUSEADDR | Three live localhost connections exchange input; immediate port reuse after shutdown; source verifies IPv4/INADDR_ANY and reuse option. |
| Maximum three clients | Three clients' messages logged; fourth closed without receiving an active slot. Disconnected slots reused. |
| Received text and exact quit | LF/CRLF and EOF framing tested, including coalesced lines, split reads, binary/empty input, long lines, reset/EOF partial input and quit lookalikes. Exact fragmented quit logs once and exits 0. |
| Signal interception/logging/clean shutdown | Every catchable signal exposed by Python's Linux signal set tested: 60 signals, exit 0, explicit signal log, lock/port free, reaped process. Four requested termination signals also close three active clients and preserve partial input. |
| Uncatchable signals | SIGSTOP suspends until caught SIGCONT; SIGKILL cannot be logged but releases the kernel-held lock. |
| Async signal safety and EINTR | Handler only records a sig_atomic_t. Test-only interposition forces EINTR in select, accept and recv; input and quit still work. |
| Cleanup and no descriptor leaks | Startup has six expected descriptors. 25 connect/disconnect cycles return to the same count. EOF, reset, quit, signals and errors release resources. |
| No busy loop or zombies | Idle CPU ticks bounded; test subreaper waits for all children. Failed daemonization reaps its child inside the parent. Final manual launch also checks system reaping. |
| Error handling | Forced mkdir, lock-file open, flock, socket, setsockopt, bind, listen, pipe, fork, setsid, chdir, dup2, sigaction, sigprocmask, select, accept and recv failures checked. Descriptor exhaustion checks logfile-open and pipe errors; actual occupied-port failure also checked. |
| Coplien equivalent and explainability | Both resource-owning classes privately declare undefined copy constructors and assignment operators for C++98; destructors release resources. README explains each system call, success/failure behavior and ownership. |
| Final build and Git checkpoints | Build/cleanup targets verified; phase checkpoints committed. Implementation, tests and evaluation notes included in Git. |

## Reproduce

```sh
make re
sudo ./tests/test_mandatory.sh
sudo ./Matt_daemon
ps aux | grep '[M]att_daemon'
sudo ss -ltnp 'sport = :4242'
sudo ls -l /var/lock/matt_daemon.lock
printf 'hello\ntest\nquit\n' | nc localhost 4242
sudo tail -20 /var/log/matt_daemon/matt_daemon.log
sudo ./Matt_daemon
sudo kill -TERM "$(pgrep -x Matt_daemon)"
make fclean
make
git status
```

Messages need a newline or EOF because TCP does not preserve write boundaries.
This keeps exact-command detection deterministic across fragmented reads.
Tests preserve previous system log content and leave the daemon stopped.
