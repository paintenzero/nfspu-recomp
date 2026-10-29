/* Enough of a BSD socket header for win32hle/wsock32.c on Windows.
 * socket()/close()/poll() go through an fd table (Winsock handles are not
 * small ints). Included only when compiling wsock32.c. */
#ifndef NFSPU_SYS_SOCKET_H
#define NFSPU_SYS_SOCKET_H
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include <winsock2.h>
#include <ws2tcpip.h>
#include <stddef.h>

#ifndef MSG_DONTWAIT
#define MSG_DONTWAIT 0x40
#endif

int win_socket(int domain, int type, int protocol);
int win_bind(int fd, const void *addr, int len);
int win_connect(int fd, const void *addr, int len);
int win_listen(int fd, int backlog);
int win_accept(int fd, void *addr, int *len);
int win_send(int fd, const void *buf, int len, int flags);
int win_sendto(int fd, const void *buf, int len, int flags, const void *addr, int alen);
int win_recv(int fd, void *buf, int len, int flags);
int win_recvfrom(int fd, void *buf, int len, int flags, void *addr, int *alen);
int win_setsockopt(int fd, int level, int opt, const void *val, int len);
int win_getsockopt(int fd, int level, int opt, void *val, int *len);
int win_getsockname(int fd, void *addr, int *len);
int win_shutdown(int fd, int how);
int win_ioctl(int fd, unsigned long req, void *arg);
int win_close_fd(int fd);
int win_poll(struct pollfd *fds, unsigned long nfds, int timeout);
int fcntl(int fd, int cmd, ...);

#define socket      win_socket
#define bind        win_bind
#define connect     win_connect
#define listen      win_listen
#define accept      win_accept
#define send        win_send
#define sendto      win_sendto
#define recv        win_recv
#define recvfrom    win_recvfrom
#define setsockopt  win_setsockopt
#define getsockopt  win_getsockopt
#define getsockname win_getsockname
#define shutdown    win_shutdown
#define ioctl       win_ioctl
#define close       win_close_fd
#define poll        win_poll
#endif
