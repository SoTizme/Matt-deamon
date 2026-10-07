# Matt_daemon Coding Plan

## Goal

Finish the **mandatory part of `Matt_daemon` using Codex as a coding partner.

**Priority:**
1. Mandatory implementation
2. Mandatory testing and evaluator-style audit
3. Bonus only if mandatory is fully working and time remains

Do **not** ask Codex to build the whole project in one pass.

Use this rule for every phase:

> Implement only this phase. Compile. Test. Stop.

---

# Project Target

Mandatory requirements to satisfy:

- Executable named `Matt_daemon`
- Must run only as root
- Must run in the background as a daemon
- Must listen on TCP port `4242`
- Must contain a `Tintin_reporter` class
- Logs go to `/var/log/matt_daemon/matt_daemon.log`
- Logs contain timestamps
- Only one daemon instance can run
- Lock file: `/var/lock/matt_daemon.lock`
- A second instance must print an error to `stderr`
- Any received text except `quit` must be logged
- Exact `quit` command must stop the daemon cleanly
- Maximum 3 simultaneous clients
- Signals must be intercepted, logged, and followed by clean shutdown
- Use `fork`, `chdir`, `flock`, and signal handling correctly
- Do **not** use `daemon()`
- Provide a proper Makefile

---

# Phase 0 — Inspect Before Coding

**Time: 00:00 → 00:15**

Give Codex:

```text
Read the Matt_daemon subject completely before modifying anything.

Our goal is the mandatory part only.
Do not implement bonuses unless I explicitly ask later.

First:

1. Inspect the existing repository.
2. Compare it against every mandatory requirement.
3. Give me a checklist of:
   - what already exists
   - what is missing
   - what is incorrect
4. Propose the smallest possible architecture.
5. Do NOT modify files yet.

Important constraints:

- Keep the project small and easy to explain.
- Do not over-engineer.
- Avoid unnecessary classes.
- Prefer simple POSIX APIs.
- Do not use daemon().
- We need to be able to explain every system call during evaluation.
- After every implementation phase, compile and test before moving on.
```

### Expected result

A minimal structure such as:

```text
Matt_daemon/
├── Makefile
├── inc/
│   ├── MattDaemon.hpp
│   └── Tintin_reporter.hpp
└── src/
    ├── main.cpp
    ├── MattDaemon.cpp
    └── Tintin_reporter.cpp
```

Do not let Codex introduce unnecessary architecture such as:

- `ClientManager`
- `SignalManager`
- `LockManager`
- `ServerFactory`
- threads
- async frameworks

---

# Phase 1 — Logger + Root Check + Lock

**Time: 00:15 → 01:00**

Give Codex:

```text
Implement Phase 1 only.

Requirements:

- executable name: Matt_daemon
- Makefile with normal all, clean, fclean, re
- reject execution unless effective UID is root
- create /var/log/matt_daemon/ if necessary
- implement class Tintin_reporter
- write logs to /var/log/matt_daemon/matt_daemon.log
- timestamp format must match the subject
- create /var/lock/matt_daemon.lock
- use flock(... LOCK_EX | LOCK_NB) so only one instance can run
- if another instance owns the lock, print the error to stderr

Keep the lock file descriptor open for the lifetime of the daemon.

Do not implement daemonization or networking yet.

When finished:

1. compile with warnings enabled
2. run basic tests
3. show me exactly what was implemented
4. list commands I should run manually
```

### Manual tests

```bash
make
./Matt_daemon
sudo ./Matt_daemon
```

Test second instance:

```bash
sudo ./Matt_daemon
sudo ./Matt_daemon
```

The second one must fail visibly.

### Git checkpoint

```bash
git add .
git commit -m "implement logging root check and daemon lock"
```

---

# Phase 2 — Daemonization

**Time: 01:00 → 01:45**

Give Codex:

```text
Implement daemonization only.

Keep the existing code working.

Use explicit POSIX calls.
Do not use daemon().

Required flow:

- acquire root/lock before detaching
- fork()
- parent exits successfully
- child creates a new session using setsid()
- chdir("/")
- redirect standard input/output/error appropriately after startup errors can no longer be needed
- keep required descriptors such as the lock and logfile usable

Add appropriate startup logs such as:

- Started
- Entering daemon mode
- daemon PID

Keep the implementation minimal.

Compile and test before finishing this phase.
```

### Test

```bash
sudo ./Matt_daemon

ps aux | grep Matt_daemon
```

Make sure:

- parent exits
- daemon continues running
- process is detached
- lock remains active

### Git checkpoint

```bash
git add .
git commit -m "implement daemonization"
```

---

# Phase 3 — TCP Server + 3 Clients + quit

**Time: 01:45 → 03:00**

This is the biggest implementation phase.

Give Codex:

```text
Implement the mandatory TCP server.

Requirements:

- IPv4 TCP socket
- listen on port 4242
- use SO_REUSEADDR
- maximum 3 simultaneous clients
- use select() for multiplexing
- do not introduce threads
- normal client messages must be written to matt_daemon.log
- if the exact command quit is received, log the quit request and terminate the daemon cleanly
- client disconnect must not crash the daemon
- a fourth simultaneous client must not become an active client

Keep all network logic simple enough to explain during evaluation.

Pay attention to:

- recv() return values
- newline handling from netcat
- disconnected sockets
- select() errors
- socket cleanup

Do not implement bonus features.

Compile and run tests after implementation.
```

## Manual networking tests

Start daemon:

```bash
sudo ./Matt_daemon
```

Check port:

```bash
sudo ss -tulpn | grep 4242
```

Connect:

```bash
nc localhost 4242
```

Send:

```text
hello
abc
test
```

Check log:

```bash
sudo tail -f /var/log/matt_daemon/matt_daemon.log
```

### Test 3 clients

Open three terminals:

```bash
nc localhost 4242
```

All three should work.

Try a fourth client.

It must not become an active client.

### Test quit

From one client:

```text
quit
```

Then:

```bash
pgrep Matt_daemon
```

The daemon should be gone.

### Git checkpoint

```bash
git add .
git commit -m "implement tcp server and client handling"
```

---

# Phase 4 — Signals + Clean Shutdown

**Time: 03:00 → 03:40**

Give Codex:

```text
Implement robust mandatory signal handling.

Prefer sigaction().

Do not perform unsafe C++ logging directly inside the signal handler.

The signal handler should only store the received signal in a volatile sig_atomic_t.

The normal daemon loop should detect it, then:

- log which signal was received
- stop accepting clients
- close all clients
- close the server socket
- release the lock
- log shutdown
- exit cleanly

Handle reasonable catchable termination signals such as:

- SIGINT
- SIGTERM
- SIGHUP
- SIGQUIT

Explain which signals cannot be caught and why.

Make sure select() returning -1 with errno == EINTR is handled correctly.

Compile and test.
```

### Signal test

Get PID:

```bash
pgrep Matt_daemon
```

Send SIGTERM:

```bash
sudo kill -15 PID
```

Check log:

```bash
sudo tail -20 /var/log/matt_daemon/matt_daemon.log
```

Verify:

- signal was logged
- daemon stopped
- sockets closed
- restart still works

### Git checkpoint

```bash
git add .
git commit -m "implement signal handling and clean shutdown"
```

---

# Phase 5 — Codex Becomes the Evaluator

**Time: 03:40 → 04:30**

Stop adding features.

Give Codex:

```text
Stop implementing features.

Act as a strict 42 evaluator for Matt_daemon.

Read the subject again and audit the complete repository.

Check every mandatory requirement one by one.

Look especially for:

- root handling
- daemonization
- forbidden daemon() usage
- fork behavior
- setsid behavior
- chdir
- single-instance flock
- stale lock behavior
- logging format and path
- errors opening log or lock
- port 4242
- bind/listen failures
- maximum 3 clients
- fourth client behavior
- disconnected clients
- exact quit handling
- signal handling
- EINTR
- descriptor leaks
- zombie processes
- CPU busy loops
- cleanup
- restart after shutdown
- warnings
- Makefile rules
- crashes and unexpected exits

Do not refactor code just for style.

First give me a severity-ranked list:

- FAIL
- RISK
- NICE TO HAVE

Fix only FAIL and important RISK items.

After fixes, rebuild and rerun the relevant tests.
```

### Important

Do not accept large rewrites now.

If Codex proposes a major architecture refactor, reject it unless it fixes a real mandatory failure.

---

# Phase 6 — Automated Mandatory Test Script

**Time: 04:30 → 04:50**

Give Codex:

```text
Create a temporary local shell script for testing Matt_daemon.

It should verify:

- build succeeds
- root requirement
- daemon starts
- process exists
- lock exists
- port 4242 is listening
- second instance fails
- normal text is logged
- three client connections work
- fourth connection is handled correctly
- quit terminates daemon
- restart works afterward
- SIGTERM produces clean shutdown

Do not modify project behavior to make the test pass.

Show the result of every test clearly as PASS or FAIL.

If something fails, explain the exact reason before changing code.
```

Run the script and fix only real failures.

---

# Phase 7 — Final 10 Minutes

**Time: 04:50 → 05:00**

Run final checks manually:

```bash
make re
```

```bash
sudo ./Matt_daemon
```

```bash
ps aux | grep Matt_daemon
```

```bash
sudo ss -tulpn | grep 4242
```

```bash
sudo ls -l /var/lock/matt_daemon.lock
```

```bash
sudo tail -20 /var/log/matt_daemon/matt_daemon.log
```

Test connection:

```bash
nc localhost 4242
```

Send:

```text
hello
test
quit
```

Restart:

```bash
sudo ./Matt_daemon
```

Signal test:

```bash
sudo kill -15 $(pgrep Matt_daemon)
```

Final build check:

```bash
make fclean
make
```

Check Git:

```bash
git status
```

Then final commit:

```bash
git add .
git commit -m "complete Matt_daemon mandatory"
```

---

# Bonus Decision

Do **not** start bonus work unless:

- every mandatory test passes
- no crashes
- second instance protection works
- signal shutdown works
- restart works
- 3-client limit works
- logs are correct
- no build warnings
- at least 15–20 minutes remain

If all of that is true, ask Codex:

```text
Mandatory is complete and all tests pass.

We have less than 20 minutes.

Review the bonus list and recommend ONLY a bonus that:

- can be implemented independently
- does not change networking behavior
- has very low risk of breaking mandatory
- can be finished and tested in under 15 minutes

Do not implement it yet.
```

The safest possible bonus candidate is likely **simple log archival/rotation**.

Avoid today:

- graphical client
- remote shell
- authentication
- encryption
- email
- public/private key systems

---

# Codex Rules for the Whole Sprint

## Rule 1 — One phase at a time

Never:

```text
Build the entire Matt_daemon project.
```

Use:

```text
Implement only Phase X.
Compile.
Test.
Report what changed.
Stop.
```

## Rule 2 — No unnecessary refactoring

Tell Codex:

```text
Do not refactor working code unless the current implementation causes a mandatory failure or serious reliability problem.
```

## Rule 3 — Require explanations

After system-level code, ask:

```text
Explain this implementation in simple terms as if I need to defend it during a 42 evaluation.

Explain:

- why this syscall is used
- what happens on success
- what happens on failure
- which file descriptor stays open
- how cleanup works
```

Focus especially on:

- `fork`
- `setsid`
- `chdir`
- `open`
- `flock`
- `socket`
- `bind`
- `listen`
- `accept`
- `select`
- `recv`
- `sigaction`

## Rule 4 — Commit after stable phases

Suggested commits:

```bash
git commit -m "implement logging root check and daemon lock"

git commit -m "implement daemonization"

git commit -m "implement tcp server and client handling"

git commit -m "implement signal handling and clean shutdown"

git commit -m "fix mandatory edge cases"

git commit -m "complete Matt_daemon mandatory"
```

## Rule 5 — Mandatory before bonus

The order for today is:

```text
inspect
   ↓
logger + root + lock
   ↓
daemonization
   ↓
TCP server
   ↓
3-client limit
   ↓
quit
   ↓
signals
   ↓
cleanup
   ↓
evaluator audit
   ↓
automated tests
   ↓
final manual tests
   ↓
bonus only if everything is green
```

---

# Final Success Checklist

Before considering the project finished:

- [ ] `make` works
- [ ] executable is exactly `Matt_daemon`
- [ ] non-root execution is rejected
- [ ] root execution works
- [ ] process becomes a background daemon
- [ ] `daemon()` is not used
- [ ] `fork()` is used correctly
- [ ] `chdir()` is used correctly
- [ ] `/var/lock/matt_daemon.lock` exists while running
- [ ] second instance is rejected
- [ ] second-instance error appears in terminal
- [ ] `/var/log/matt_daemon/matt_daemon.log` exists
- [ ] log timestamps are correct
- [ ] server listens on `4242`
- [ ] normal messages are logged
- [ ] exactly 3 clients can be active
- [ ] fourth client is handled safely
- [ ] client disconnect does not crash server
- [ ] `quit` shuts daemon down
- [ ] signals are intercepted
- [ ] received signal is logged
- [ ] shutdown closes client sockets
- [ ] shutdown closes server socket
- [ ] lock is released
- [ ] daemon can restart after shutdown
- [ ] no busy loop
- [ ] no zombie process
- [ ] no compiler warnings/errors
- [ ] Makefile rules work
- [ ] code can be explained during evaluation

---

# Target

> **A small, understandable, tested mandatory implementation is better than a larger project with unfinished bonuses.**
