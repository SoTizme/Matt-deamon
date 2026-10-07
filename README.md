# Matt_daemon

Phase 1 of `matt_daemon_plan.md` is implemented: root check, logger and exclusive
lock. Daemonization, networking and signal handling are pending. No bonuses are
implemented.

## Build and run

```sh
make
./Matt_daemon       # fails if effective UID is not root
sudo ./Matt_daemon  # stays in the foreground; press Enter to stop
```

While the root process is waiting, run `sudo ./Matt_daemon` in another terminal.
It must exit unsuccessfully and print a lock error to standard error. After
pressing Enter in the first terminal, starting again must succeed.

```sh
sudo tail -10 /var/log/matt_daemon/matt_daemon.log
make clean
make fclean
make re
sudo python3 tests/test_phase1.py
```

The test script uses the real system log and lock paths. It requires an idle
Matt_daemon lock, preserves existing logs, and stops the processes it starts.

## How Phase 1 works

`main` checks `geteuid()` before creating any files. `MattDaemon` opens
`/var/lock/matt_daemon.lock` and calls `flock(LOCK_EX | LOCK_NB)`. The exclusive
lock fails immediately if another process owns it. Both open and lock failures
produce a path-specific error on standard error and a nonzero exit status.

The lock descriptor stays open for the lifetime of `MattDaemon`. Its destructor
closes the descriptor, releasing the lock even when startup throws an exception.
The lock file is left in place: an existing unlocked file can be reused safely,
and retaining it avoids races caused by unlinking an inode another process has
already opened. File existence alone does not indicate that a process is running.

`Tintin_reporter` creates the log directory if necessary, checks that it is a
directory, and opens the logfile for append. Each entry uses the subject's
example format, `[DD/MM/YYYY-HH:MM:SS] [ INFO ] - Matt_daemon: message`, and is
flushed immediately. Errors creating the directory, opening or writing the log,
or formatting a timestamp propagate to `main` for reporting. The output stream
closes automatically when the reporter is destroyed. Both classes disable copying
so their resources have one owner (C++11 rule-of-three equivalent).

The foreground Enter wait is temporary scaffolding to exercise lock lifetime;
it will be replaced in the next implementation phase.

AI assistance was used to implement Phase 1 and its tests; review the system calls
and ownership rules above before evaluation.
