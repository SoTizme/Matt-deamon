#!/usr/bin/env python3
"""Phase 1 integration checks. Run as root on Linux with no instance running."""
import fcntl
import os
from pathlib import Path
import re
import resource
import selectors
import shutil
import tempfile
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
BINARY = str(ROOT / "Matt_daemon")
LOCK = Path("/var/lock/matt_daemon.lock")
LOG = Path("/var/log/matt_daemon/matt_daemon.log")
PROCESSES = []


def check(condition, description):
    if not condition:
        raise AssertionError(description)
    print("PASS:", description, flush=True)


def launch(binary=BINARY, **kwargs):
    process = subprocess.Popen(
        [binary], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
        stderr=subprocess.PIPE, text=True, **kwargs
    )
    PROCESSES.append(process)
    return process


def start():
    process = launch()
    with selectors.DefaultSelector() as selector:
        selector.register(process.stdout, selectors.EVENT_READ)
        if not selector.select(timeout=5):
            raise AssertionError("startup did not report readiness within five seconds")
    line = process.stdout.readline()
    if "lock acquired" not in line or process.poll() is not None:
        raise AssertionError("startup failed: " + process.stderr.read())
    return process


def stop(process):
    _, stderr = process.communicate(input="\n", timeout=5)
    if process.returncode != 0 or stderr:
        raise AssertionError("clean foreground shutdown failed: " + stderr)


def non_root():
    os.setgroups([])
    os.setgid(65534)
    # Retain real UID 0: the program must specifically reject a non-root EUID.
    os.setresuid(0, 65534, 65534)


def no_log_descriptor():
    # stdin/stdout/stderr plus lock fit; opening the log cannot fit.
    resource.setrlimit(resource.RLIMIT_NOFILE, (4, 4))


def lock_is_free():
    with LOCK.open("a") as handle:
        fcntl.flock(handle, fcntl.LOCK_EX | fcntl.LOCK_NB)


def main():
    if os.geteuid() != 0:
        raise RuntimeError("run this script as root")
    subprocess.run(["make"], cwd=ROOT, check=True, timeout=30)
    check(Path(BINARY).is_file(), "build produces Matt_daemon")
    if LOCK.exists():
        lock_is_free()  # Refuse to interfere with an existing instance.
    previous_log = LOG.read_bytes() if LOG.exists() else b""
    previous_lock = LOCK.stat() if LOCK.exists() else None

    # The repository may live under a private home directory.
    with tempfile.TemporaryDirectory(prefix="matt_phase1_") as directory:
        os.chmod(directory, 0o755)
        accessible_binary = str(Path(directory) / "Matt_daemon")
        shutil.copy2(BINARY, accessible_binary)
        rejected = launch(binary=accessible_binary, preexec_fn=non_root)
        stdout, stderr = rejected.communicate(timeout=5)
    check(rejected.returncode != 0 and "root privileges required" in stderr
          and not stdout, "non-root effective UID rejected on stderr")
    check((LOG.read_bytes() if LOG.exists() else b"") == previous_log
          and (LOCK.stat() if LOCK.exists() else None) == previous_lock,
          "root rejection does not change the log or lock")

    first = start()
    check(LOCK.is_file() and LOG.is_file(), "root startup creates log and lock")
    with LOCK.open("a") as handle:
        try:
            fcntl.flock(handle, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError:
            pass
        else:
            raise AssertionError("running process is not holding the lock")
    check(first.poll() is None, "exclusive lock held while foreground process waits")

    competitors = [launch() for _ in range(4)]
    for process in competitors:
        stdout, stderr = process.communicate(timeout=5)
        check(process.returncode != 0 and not stdout
              and "Can't open :/var/lock/matt_daemon.lock" in stderr
              and "exclusive lock" in stderr,
              "competing instance fails immediately with visible lock error")

    stop(first)
    lock_is_free()
    check(True, "Enter shuts down cleanly and releases the lock")
    restarted = start()
    stop(restarted)
    check(True, "restart succeeds with existing unlocked lock file")

    failing = launch(preexec_fn=no_log_descriptor)
    stdout, stderr = failing.communicate(timeout=5)
    check(failing.returncode != 0 and not stdout
          and "Can't open :/var/log/matt_daemon/matt_daemon.log" in stderr,
          "log-open failure reported on stderr")
    lock_is_free()
    recovered = start()
    stop(recovered)
    check(True, "startup failure leaves lock free and restart succeeds")

    current_log = LOG.read_bytes()
    check(current_log.startswith(previous_log), "existing log contents preserved")
    lines = current_log[len(previous_log):].decode().splitlines()
    pattern = r"\[\d{2}/\d{2}/\d{4}-\d{2}:\d{2}:\d{2}\] \[ INFO \] - Matt_daemon: (Started|Quitting)\."
    check(len(lines) == 6 and all(re.fullmatch(pattern, line) for line in lines),
          "three successful runs append timestamped startup and shutdown entries")
    check([line.rsplit(": ", 1)[-1] for line in lines]
          == ["Started.", "Quitting."] * 3,
          "competing and failed instances add no startup entries")


if __name__ == "__main__":
    try:
        main()
    except Exception as error:
        print("FAIL:", error, file=sys.stderr)
        sys.exit(1)
    finally:
        for process in PROCESSES:
            if process.poll() is None:
                process.kill()
            process.communicate(timeout=5)
