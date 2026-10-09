#pragma once
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
// No actual sockets or hardware are used by the host firmware tests.
inline int fakeSetsockopt(int, int, int, const void*, socklen_t) { return 0; }
inline int fakeShutdown(int, int) { return 0; }
inline int fakeClose(int) { return 0; }
#define setsockopt fakeSetsockopt
#define shutdown fakeShutdown
#define close fakeClose
