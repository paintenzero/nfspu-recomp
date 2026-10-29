/* Small-fd Winsock wrappers so win32hle's wsock32.c can keep its POSIX calls. */
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include <winsock2.h>
#include <ws2tcpip.h>
#include <errno.h>
#include <stdlib.h>
#include <stdarg.h>
#include <io.h>
#include "wincompat/ifaddrs.h"
#include "wincompat/sys/socket.h"

#undef socket
#undef bind
#undef connect
#undef listen
#undef accept
#undef send
#undef sendto
#undef recv
#undef recvfrom
#undef setsockopt
#undef getsockopt
#undef getsockname
#undef shutdown
#undef ioctl
#undef close
#undef poll

#define MAXFD 256
static SOCKET g_sock[MAXFD];
static unsigned char g_used[MAXFD];
static int g_wsa;

static void wsa_once(void) {
    if (g_wsa) return;
    WSADATA w;
    if (WSAStartup(MAKEWORD(2, 2), &w) == 0) g_wsa = 1;
}

static void set_errno_from_wsa(void) {
    switch (WSAGetLastError()) {
    case WSAEWOULDBLOCK: errno = EWOULDBLOCK; break;
    case WSAEADDRINUSE:  errno = EADDRINUSE; break;
    case WSAEINVAL:      errno = EINVAL; break;
    case WSAECONNRESET:  errno = ECONNRESET; break;
    case WSAECONNREFUSED: errno = ECONNREFUSED; break;
    case WSAEAFNOSUPPORT: errno = EAFNOSUPPORT; break;
    case WSAEBADF: case WSAENOTSOCK: errno = EBADF; break;
    default: errno = EIO; break;
    }
}

static int alloc_fd(SOCKET s) {
    for (int i = 3; i < MAXFD; i++) {
        if (!g_used[i]) { g_used[i] = 1; g_sock[i] = s; return i; }
    }
    closesocket(s);
    errno = EMFILE;
    return -1;
}

static SOCKET sock_of(int fd) {
    if (fd < 0 || fd >= MAXFD || !g_used[fd]) { errno = EBADF; return INVALID_SOCKET; }
    return g_sock[fd];
}

int win_socket(int domain, int type, int protocol) {
    wsa_once();
    SOCKET s = socket(domain, type, protocol);
    if (s == INVALID_SOCKET) { set_errno_from_wsa(); return -1; }
    return alloc_fd(s);
}
int win_bind(int fd, const void *addr, int len) {
    int r = bind(sock_of(fd), (const struct sockaddr *)addr, len);
    if (r) set_errno_from_wsa();
    return r;
}
int win_connect(int fd, const void *addr, int len) {
    int r = connect(sock_of(fd), (const struct sockaddr *)addr, len);
    if (r) set_errno_from_wsa();
    return r;
}
int win_listen(int fd, int backlog) {
    int r = listen(sock_of(fd), backlog);
    if (r) set_errno_from_wsa();
    return r;
}
int win_accept(int fd, void *addr, int *len) {
    SOCKET s = accept(sock_of(fd), (struct sockaddr *)addr, len);
    if (s == INVALID_SOCKET) { set_errno_from_wsa(); return -1; }
    return alloc_fd(s);
}
int win_send(int fd, const void *buf, int len, int flags) {
    (void)flags;
    int r = send(sock_of(fd), (const char *)buf, len, 0);
    if (r < 0) set_errno_from_wsa();
    return r;
}
int win_sendto(int fd, const void *buf, int len, int flags, const void *addr, int alen) {
    (void)flags;
    int r = sendto(sock_of(fd), (const char *)buf, len, 0, (const struct sockaddr *)addr, alen);
    if (r < 0) set_errno_from_wsa();
    return r;
}
int win_recv(int fd, void *buf, int len, int flags) {
    (void)flags;
    int r = recv(sock_of(fd), (char *)buf, len, 0);
    if (r < 0) set_errno_from_wsa();
    return r;
}
int win_recvfrom(int fd, void *buf, int len, int flags, void *addr, int *alen) {
    (void)flags;
    int r = recvfrom(sock_of(fd), (char *)buf, len, 0, (struct sockaddr *)addr, alen);
    if (r < 0) set_errno_from_wsa();
    return r;
}
int win_setsockopt(int fd, int level, int opt, const void *val, int len) {
    int r = setsockopt(sock_of(fd), level, opt, (const char *)val, len);
    if (r) set_errno_from_wsa();
    return r;
}
int win_getsockopt(int fd, int level, int opt, void *val, int *len) {
    int r = getsockopt(sock_of(fd), level, opt, (char *)val, len);
    if (r) set_errno_from_wsa();
    return r;
}
int win_getsockname(int fd, void *addr, int *len) {
    int r = getsockname(sock_of(fd), (struct sockaddr *)addr, len);
    if (r) set_errno_from_wsa();
    return r;
}
int win_shutdown(int fd, int how) {
    int r = shutdown(sock_of(fd), how);
    if (r) set_errno_from_wsa();
    return r;
}
int win_ioctl(int fd, unsigned long req, void *arg) {
    int r = ioctlsocket(sock_of(fd), (long)req, (u_long *)arg);
    if (r) set_errno_from_wsa();
    return r;
}
int win_close_fd(int fd) {
    SOCKET s = sock_of(fd);
    if (s == INVALID_SOCKET) return -1;
    g_used[fd] = 0;
    if (closesocket(s)) { set_errno_from_wsa(); return -1; }
    return 0;
}
int win_poll(struct pollfd *fds, unsigned long nfds, int timeout) {
    fd_set r, w, e;
    FD_ZERO(&r); FD_ZERO(&w); FD_ZERO(&e);
    SOCKET maxs = 0;
    int nfd = 0;
    for (unsigned long i = 0; i < nfds; i++) {
        fds[i].revents = 0;
        SOCKET s = sock_of(fds[i].fd);
        if (s == INVALID_SOCKET) { fds[i].revents = POLLERR; continue; }
        if (fds[i].events & POLLIN) FD_SET(s, &r);
        if (fds[i].events & POLLOUT) FD_SET(s, &w);
        FD_SET(s, &e);
        if (s > maxs) maxs = s;
        nfd++;
    }
    if (!nfd) return 0;
    struct timeval tv, *tvp = NULL;
    if (timeout >= 0) { tv.tv_sec = timeout / 1000; tv.tv_usec = (timeout % 1000) * 1000; tvp = &tv; }
    int rc = select((int)maxs + 1, &r, &w, &e, tvp);
    if (rc < 0) { set_errno_from_wsa(); return -1; }
    int ready = 0;
    for (unsigned long i = 0; i < nfds; i++) {
        SOCKET s = sock_of(fds[i].fd);
        if (s == INVALID_SOCKET) continue;
        if (FD_ISSET(s, &r)) fds[i].revents |= POLLIN;
        if (FD_ISSET(s, &w)) fds[i].revents |= POLLOUT;
        if (FD_ISSET(s, &e)) fds[i].revents |= POLLERR;
        if (fds[i].revents) ready++;
    }
    return ready;
}

int getifaddrs(struct ifaddrs **out) { if (out) *out = NULL; errno = ENOTSUP; return -1; }
void freeifaddrs(struct ifaddrs *p) { (void)p; }

int fcntl(int fd, int cmd, ...) {
    if (cmd == 3) return 0;                 /* F_GETFL */
    if (cmd == 4) {                         /* F_SETFL */
        va_list ap;
        va_start(ap, cmd);
        int fl = va_arg(ap, int);
        va_end(ap);
        u_long nb = (fl & 04000) ? 1 : 0;   /* O_NONBLOCK */
        SOCKET s = sock_of(fd);
        if (s == INVALID_SOCKET) return -1;
        if (ioctlsocket(s, FIONBIO, &nb)) { set_errno_from_wsa(); return -1; }
        return 0;
    }
    errno = EINVAL;
    return -1;
}
