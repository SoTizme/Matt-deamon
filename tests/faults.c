#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

/* Test-only interposition: production code never reads MATT_TEST_FAIL. */
static int failed(const char *name) {
    const char *requested = getenv("MATT_TEST_FAIL");
    static int interrupted;
    char transient[64];
    if (!requested)
        return 0;
    if (strcmp(requested, name) == 0) {
        errno = EIO;
        return 1;
    }
    snprintf(transient, sizeof(transient), "%s_eintr", name);
    if (strcmp(requested, transient) == 0 && !interrupted++) {
        errno = EINTR;
        return 1;
    }
    return 0;
}

#define WRAP(type, name, args, call) \
    type name args { \
        static type (*real) args; \
        if (failed(#name)) return (type)-1; \
        if (!real) real = dlsym(RTLD_NEXT, #name); \
        return real call; \
    }

WRAP(int, mkdir, (const char *p, mode_t mode), (p, mode))
WRAP(int, flock, (int fd, int operation), (fd, operation))
WRAP(int, socket, (int domain, int type, int protocol), (domain, type, protocol))
WRAP(int, setsockopt, (int fd, int level, int opt, const void *v, socklen_t n), (fd, level, opt, v, n))
WRAP(int, bind, (int fd, const struct sockaddr *a, socklen_t n), (fd, a, n))
WRAP(int, listen, (int fd, int n), (fd, n))
WRAP(int, pipe, (int p[2]), (p))
WRAP(pid_t, fork, (void), ())
WRAP(pid_t, setsid, (void), ())
WRAP(int, chdir, (const char *p), (p))
WRAP(int, dup2, (int oldfd, int newfd), (oldfd, newfd))
WRAP(int, sigaction, (int s, const struct sigaction *a, struct sigaction *o), (s, a, o))
WRAP(int, sigprocmask, (int how, const sigset_t *s, sigset_t *o), (how, s, o))
WRAP(int, select, (int n, fd_set *r, fd_set *w, fd_set *e, struct timeval *t), (n, r, w, e, t))
WRAP(int, accept, (int fd, struct sockaddr *a, socklen_t *n), (fd, a, n))
WRAP(ssize_t, recv, (int fd, void *b, size_t n, int flags), (fd, b, n, flags))
