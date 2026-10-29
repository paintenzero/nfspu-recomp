#ifndef NFSPU_IFADDRS_H
#define NFSPU_IFADDRS_H
struct ifaddrs { struct ifaddrs *ifa_next; char *ifa_name; unsigned ifa_flags; struct sockaddr *ifa_addr; };
int getifaddrs(struct ifaddrs **out);
void freeifaddrs(struct ifaddrs *p);
#endif
