#!/usr/bin/env python3
"""Linux/root integration tests. Preserve logs; stop only daemons started here."""
import ctypes
import fcntl
import os
from pathlib import Path
import pwd
import re
import resource
import shutil
import signal
import socket
import struct
import subprocess
import sys
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]
BINARY = str(ROOT / "Matt_daemon")
LOCK = Path("/var/lock/matt_daemon.lock")
LOG = Path("/var/log/matt_daemon/matt_daemon.log")
OWNED = set()
CLIENTS = []
PASSES = 0


def require(condition, description):
    if not condition:
        raise AssertionError(description)


def passed(description):
    global PASSES
    PASSES += 1
    print("PASS:", description, flush=True)


def wait_until(test, description, timeout=5):
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if test():
            return
        time.sleep(0.02)
    raise AssertionError(description)


def log_after(offset):
    return LOG.read_bytes()[offset:].decode(errors="replace")


def wait_log(offset, text):
    wait_until(lambda: text in log_after(offset), "missing log: " + text)


def invoke(**kwargs):
    return subprocess.run(
        [BINARY], stdin=subprocess.DEVNULL, capture_output=True,
        text=True, timeout=5, **kwargs
    )


def start(**kwargs):
    offset = LOG.stat().st_size if LOG.exists() else 0
    result = invoke(**kwargs)
    require(result.returncode == 0 and not result.stdout and not result.stderr,
            "startup failed: " + result.stderr)
    match = re.search(r"started\. PID: (\d+)\.", log_after(offset))
    require(match is not None, "startup did not log a PID")
    pid = int(match[1])
    OWNED.add(pid)
    return pid, offset


def reap(pid, expected=0):
    result = []
    def exited():
        waited, status = os.waitpid(pid, os.WNOHANG)
        if waited:
            result.append(os.waitstatus_to_exitcode(status))
            OWNED.discard(pid)
            return True
        return False
    wait_until(exited, "daemon did not stop within five seconds")
    require(result == [expected], "unexpected exit status: " + repr(result))
    require(not Path("/proc", str(pid)).exists(), "child remains as a zombie")


def connect():
    client = socket.create_connection(("127.0.0.1", 4242), timeout=3)
    CLIENTS.append(client)
    return client


def expect_closed(client):
    try:
        require(client.recv(1) == b"", "client remained open")
    except ConnectionResetError:
        pass


def lock_free():
    with LOCK.open("a") as handle:
        fcntl.flock(handle, fcntl.LOCK_EX | fcntl.LOCK_NB)


def lock_held():
    with LOCK.open("a") as handle:
        try:
            fcntl.flock(handle, fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError:
            return
    raise AssertionError("daemon lost its singleton lock")


def port_free():
    with socket.socket() as probe:
        probe.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        probe.bind(("0.0.0.0", 4242))


def terminate(pid, offset, number=signal.SIGTERM):
    os.kill(pid, number)
    reap(pid)
    text = log_after(offset)
    require("Signal received: " + str(int(number)) + " (" in text,
            "signal number was not logged")
    require("Server closed." in text and "Quitting." in text,
            "shutdown was not logged")
    lock_free()
    port_free()


def non_root():
    os.setgroups([])
    os.setgid(65534)
    # Deliberately retain real UID 0 to verify the effective-UID check.
    os.setresuid(0, 65534, 65534)


def build():
    # sudo must not leave root-owned build files in the user's checkout.
    owner = pwd.getpwuid(ROOT.stat().st_uid)
    def as_owner():
        os.setgroups([])
        os.setgid(owner.pw_gid)
        os.setuid(owner.pw_uid)
    def make(*args):
        subprocess.run(["make", *args], cwd=ROOT, check=True, timeout=30,
                       preexec_fn=as_owner)
    make("re")
    require(Path(BINARY).is_file(), "executable name is incorrect")
    timestamp = Path(BINARY).stat().st_mtime_ns
    make()
    require(Path(BINARY).stat().st_mtime_ns == timestamp, "make relinks needlessly")
    object_files = [ROOT / name for name in [
        "main.o", "include/Tintin_reporter.o", "daemon/MattDaemon.o", "daemon/Server.o"
    ]]
    header = ROOT / "daemon/MattDaemon.hpp"
    previous_times = (header.stat().st_atime_ns, header.stat().st_mtime_ns)
    try:
        time.sleep(0.02)
        header.touch()
        make()
        require(Path(BINARY).stat().st_mtime_ns != timestamp,
                "header dependency did not rebuild the executable")
    finally:
        os.utime(header, ns=previous_times)
    make("clean")
    require(Path(BINARY).exists()
            and all(not p.exists() for p in object_files), "clean rule")
    make("fclean")
    require(not Path(BINARY).exists(), "fclean rule")
    make("-j4")
    require(all(not p.with_suffix(".d").exists() for p in object_files),
            "build generated unwanted .d files")
    passed("all, clean, fclean, re, parallel build, incremental make and header dependencies")
    source_files = [ROOT / "main.cpp"]
    for directory in ["daemon", "include"]:
        source_files.extend((ROOT / directory).glob("*.cpp"))
    require(len(source_files) == 4, "production source layout is incomplete")
    sources = "".join(p.read_text() for p in source_files)
    require("-std=c++98" in (ROOT / "Makefile").read_text(), "C++98 flag missing")
    require(not re.search(r"\bdaemon\s*\(", sources), "forbidden daemon() call")
    passed("C++98 build uses warnings as errors and no daemon()")


def root_and_network():
    previous = LOG.read_bytes() if LOG.exists() else b""
    prior_lock = LOCK.stat() if LOCK.exists() else None
    with tempfile.TemporaryDirectory(prefix="matt_nonroot_") as directory:
        os.chmod(directory, 0o755)
        binary = str(Path(directory) / "Matt_daemon")
        shutil.copy2(BINARY, binary)
        rejected = subprocess.run([binary], capture_output=True, text=True,
                                  preexec_fn=non_root, timeout=5)
    require(rejected.returncode != 0 and not rejected.stdout
            and "root privileges required" in rejected.stderr, "root requirement")
    require((LOG.read_bytes() if LOG.exists() else b"") == previous
            and (LOCK.stat() if LOCK.exists() else None) == prior_lock,
            "non-root execution changed files")
    passed("non-root effective UID rejected on stderr before filesystem changes")

    pid, offset = start()
    proc = Path("/proc", str(pid))
    fields = (proc / "stat").read_text().split()
    require(int(fields[4]) == pid and int(fields[5]) == pid and int(fields[6]) == 0,
            "process did not become a detached session leader")
    require(os.readlink(proc / "cwd") == "/", "daemon working directory")
    require(all(os.readlink(proc / "fd" / str(fd)) == "/dev/null" for fd in range(3)),
            "standard-stream redirection")
    uid_line = next(line for line in (proc / "status").read_text().splitlines()
                    if line.startswith("Uid:"))
    require(uid_line.split()[2] == "0", "daemon effective UID")
    links = [os.readlink(fd) for fd in (proc / "fd").iterdir()]
    require(str(LOCK.resolve()) in links and str(LOG.resolve()) in links,
            "required resource descriptors were lost")
    lock_held()
    baseline = len(links)
    require(baseline == 6, "startup pipe or null descriptor leaked")
    passed("parent exits; root child detached; cwd /; stdio redirected; required descriptors held")

    for _ in range(4):
        result = invoke()
        require(result.returncode != 0 and not result.stdout
                and "Can't open :/var/lock/matt_daemon.lock" in result.stderr
                and "exclusive lock" in result.stderr, "second-instance error")
    passed("four competing launches rejected immediately on stderr")

    clients = [connect() for _ in range(3)]
    wait_until(lambda: log_after(offset).count("Client connected.") == 3,
               "three clients were not accepted")
    for index, client in enumerate(clients):
        client.sendall(("client-" + str(index) + "\n").encode())
    for index in range(3):
        wait_log(offset, "User input: client-" + str(index))
    passed("port 4242 logs all three simultaneous clients")
    fourth = connect()
    expect_closed(fourth)
    fourth.close()
    require(log_after(offset).count("Client connected.") == 3, "fourth became active")
    passed("fourth client rejected safely")

    clients[0].sendall(b"hello\r\nabc\ntest\nquite\n quit\nquit \nQUIT\n\x00binary\n\n")
    for value in ["hello", "abc", "test", "quite", " quit", "quit ", "QUIT", "\x00binary"]:
        wait_log(offset, "User input: " + value)
    clients[1].sendall(b"qu")
    time.sleep(0.05)
    clients[1].sendall(b"itter\n")
    wait_log(offset, "User input: quitter")
    require(pid in OWNED and proc.exists(), "quit lookalike stopped daemon")
    passed("CRLF, multiple lines, empty/binary input and fragmented quit lookalikes")

    clients[2].sendall(b"no-final-newline")
    clients[2].shutdown(socket.SHUT_WR)
    wait_log(offset, "User input: no-final-newline")
    expect_closed(clients[2])
    clients[2].close()
    wait_until(lambda: len(list((proc / "fd").iterdir())) == baseline + 2,
               "disconnected descriptor was not released")
    passed("EOF logs partial input and frees client slot")

    for index in range(25):
        churn = connect()
        marker = "churn-" + str(index)
        churn.sendall((marker + "\n").encode())
        wait_log(offset, "User input: " + marker)
        churn.close()
        wait_until(lambda: len(list((proc / "fd").iterdir())) == baseline + 2,
                   "connection churn leaked descriptors")
    passed("25 connect/disconnect cycles reuse slots without descriptor leaks")

    reset = connect()
    reset.sendall(b"reset-ready\nreset-partial")
    wait_log(offset, "User input: reset-ready")
    time.sleep(0.02)
    reset.setsockopt(socket.SOL_SOCKET, socket.SO_LINGER, struct.pack("ii", 1, 0))
    reset.close()
    wait_log(offset, "User input: reset-partial")
    passed("connection reset preserves received partial input")

    clients[0].sendall(b"X" * 65536 + b"quit\n" + b"Y" * 140000 + b"\nlong-end\n")
    wait_log(offset, "User input: long-end")
    text = log_after(offset)
    payloads = [line.split("User input: ", 1)[1] for line in text.splitlines()
                if "User input: " in line]
    require("X" * 65536 in payloads and "quit" in payloads,
            "long input chunk was discarded or treated as command")
    require(sum(len(line) for line in payloads if line and set(line) == {"Y"}) == 140000,
            "long input lost bytes")
    require("Request quit." not in text, "long message suffix triggered quit")
    passed("long messages logged in bounded chunks; trailing quit stays ordinary text")

    ticks_before = sum(map(int, (proc / "stat").read_text().split()[13:15]))
    time.sleep(0.4)
    ticks_after = sum(map(int, (proc / "stat").read_text().split()[13:15]))
    require(ticks_after - ticks_before <= 5, "daemon busy-loops while idle")
    passed("idle daemon blocks instead of busy-looping")

    clients[0].sendall(b"qu")
    time.sleep(0.05)
    clients[0].sendall(b"it\r\n")
    reap(pid)
    for client in clients[:2]:
        expect_closed(client)
        client.close()
    text = log_after(offset)
    require(text.count("Request quit.") == 1 and "Quitting." in text,
            "quit request/shutdown missing or duplicated")
    lock_free()
    port_free()
    passed("fragmented exact quit closes clients/server, logs once and releases lock")

    pid, offset = start()
    raw = connect()
    raw.sendall(b"quit")
    raw.shutdown(socket.SHUT_WR)
    reap(pid)
    expect_closed(raw)
    raw.close()
    require("Request quit." in log_after(offset), "EOF-terminated quit not recognized")
    passed("immediate restart and exact quit without newline at EOF")
    require(LOG.read_bytes().startswith(previous), "existing log was overwritten")
    lines = log_after(len(previous)).splitlines()
    require(all(re.match(r"\[\d{2}/\d{2}/\d{4}-\d{2}:\d{2}:\d{2}\] \[ (INFO|LOG|ERROR) \] - Matt_daemon: ", line)
                for line in lines), "log timestamp/entry format")
    passed("subject timestamp format; existing logs preserved")


def signal_tests():
    for number in sorted(signal.valid_signals() - {signal.SIGKILL, signal.SIGSTOP}):
        pid, offset = start()
        active = []
        if number in {signal.SIGINT, signal.SIGTERM, signal.SIGHUP, signal.SIGQUIT}:
            active = [connect() for _ in range(3)]
            wait_until(lambda: log_after(offset).count("Client connected.") == 3,
                       "signal clients not accepted")
            active[0].sendall(b"signal-ready\nsignal-partial")
            wait_log(offset, "User input: signal-ready")
            time.sleep(0.02)
        terminate(pid, offset, number)
        for client in active:
            expect_closed(client)
            client.close()
        if active:
            require("User input: signal-partial" in log_after(offset),
                    "signal shutdown discarded received text")
        passed("signal " + str(int(number)) + ": logged, exit 0, lock/port free, no zombie")

    pid, offset = start()
    os.kill(pid, signal.SIGSTOP)
    wait_until(lambda: Path("/proc", str(pid), "stat").read_text().split()[2] == "T",
               "SIGSTOP did not suspend process")
    lock_held()
    terminate(pid, offset, signal.SIGCONT)
    passed("uncatchable SIGSTOP suspends; caught SIGCONT shuts down")

    pid, _ = start()
    os.kill(pid, signal.SIGKILL)
    reap(pid, -signal.SIGKILL)
    lock_free()
    pid, offset = start()
    terminate(pid, offset)
    passed("uncatchable SIGKILL leaves reusable stale lock; restart succeeds")


def startup_regressions():
    def closed_stdio():
        for fd in range(3):
            os.close(fd)
    def blocked_term():
        signal.pthread_sigmask(signal.SIG_BLOCK, {signal.SIGTERM})
    for prepare in [closed_stdio, blocked_term]:
        pid, offset = start(preexec_fn=prepare)
        lock_held()
        terminate(pid, offset)
        passed("startup with " + prepare.__name__ + " retains lock and handles SIGTERM")

    with socket.socket() as occupied:
        occupied.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        occupied.bind(("0.0.0.0", 4242))
        occupied.listen(1)
        result = invoke()
        require(result.returncode != 0 and "bind port 4242" in result.stderr,
                "occupied port did not fail visibly")
    lock_free()
    passed("occupied port fails before detaching and releases lock")

    for limit, error in [(4, "Can't open :/var/log/matt_daemon/matt_daemon.log"), (7, "pipe")]:
        def limited():
            resource.setrlimit(resource.RLIMIT_NOFILE, (limit, limit))
        result = invoke(preexec_fn=limited)
        require(result.returncode != 0 and error in result.stderr,
                "descriptor-limit startup error not reported: " + result.stderr)
        lock_free()
        port_free()
        passed("descriptor exhaustion reports " + error + " and cleans resources")


def fault_tests():
    with tempfile.TemporaryDirectory(prefix="matt_faults_") as directory:
        library = str(Path(directory) / "faults.so")
        subprocess.run(["cc", "-shared", "-fPIC", "-Wall", "-Wextra", "-Werror",
                        str(ROOT / "tests/faults.c"), "-ldl", "-o", library],
                       check=True, timeout=30)
        def environment(name):
            return dict(os.environ, LD_PRELOAD=library, MATT_TEST_FAIL=name)
        for name in ["mkdir", "open", "flock", "socket", "setsockopt", "bind", "listen",
                     "pipe", "fork", "setsid", "chdir", "dup2", "sigaction", "sigprocmask"]:
            offset = LOG.stat().st_size
            result = invoke(env=environment(name))
            require(result.returncode != 0 and result.stderr, name + " startup reported success")
            if name == "open":
                require("Can't open :/var/lock/matt_daemon.lock" in result.stderr,
                        "lock-open failure did not name its path")
            if name not in {"mkdir", "open", "flock"}:
                require("[ ERROR ]" in log_after(offset), name + " failure not logged")
            lock_free()
            port_free()
            try:
                os.waitpid(-1, os.WNOHANG)
            except ChildProcessError:
                pass
            else:
                raise AssertionError("startup failure left an unexpected child")
            passed("forced " + name + " failure: visible startup error, clean resources")
        for name in ["select", "accept"]:
            pid, offset = start(env=environment(name))
            if name == "accept":
                client = connect()
            reap(pid, 1)
            require(name + ":" in log_after(offset), name + " failure not logged")
            lock_free()
            port_free()
            passed("forced " + name + " failure: logged error, clean exit 1")
        pid, offset = start(env=environment("recv"))
        client = connect()
        client.sendall(b"force-recv\n")
        expect_closed(client)
        client.close()
        wait_log(offset, "recv:")
        terminate(pid, offset)
        passed("forced recv failure closes client without crashing server")
        for name in ["select_eintr", "accept_eintr", "recv_eintr"]:
            pid, offset = start(env=environment(name))
            client = connect()
            client.sendall((name + "\n").encode())
            wait_log(offset, "User input: " + name)
            client.sendall(b"quit\n")
            reap(pid)
            client.close()
            lock_free()
            port_free()
            passed(name + " retries successfully without losing client input")


def main():
    require(os.geteuid() == 0, "run this suite with sudo")
    require(sys.platform.startswith("linux"), "Linux is required")
    if LOCK.exists():
        lock_free()  # Refuse to interfere with another daemon.
    port_free()
    require(ctypes.CDLL(None).prctl(36, 1, 0, 0, 0) == 0,
            "cannot become a test subreaper")
    build()
    root_and_network()
    signal_tests()
    startup_regressions()
    fault_tests()
    lock_free()
    port_free()
    passed("final state: no test daemon, lock free, port free")
    print(str(PASSES) + " checks passed.", flush=True)


if __name__ == "__main__":
    try:
        main()
    except Exception as error:
        print("FAIL:", error, file=sys.stderr, flush=True)
        sys.exit(1)
    finally:
        for client in CLIENTS:
            client.close()
        for pid in list(OWNED):
            try:
                os.kill(pid, signal.SIGKILL)
            except ProcessLookupError:
                pass
            try:
                os.waitpid(pid, 0)
            except ChildProcessError:
                pass
