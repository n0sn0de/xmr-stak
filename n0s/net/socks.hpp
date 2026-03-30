#pragma once

/* POSIX sockets */
#include <arpa/inet.h>
#include <cerrno>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <cstring>
#include <sys/socket.h>
#include <unistd.h>

inline void sock_init() {}
typedef int SOCKET;

#define INVALID_SOCKET (-1)
#define SOCKET_ERROR (-1)

inline void sock_close(SOCKET s)
{
	shutdown(s, SHUT_RDWR);
	close(s);
}

inline const char* sock_strerror(char* buf, size_t len)
{
	buf[0] = '\0';
	return strerror_r(errno, buf, len);
}

inline const char* sock_gai_strerror(int err, char* buf, [[maybe_unused]] size_t len)
{
	buf[0] = '\0';
	return gai_strerror(err);
}
