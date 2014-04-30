#include "log.h"
#include "net.h"
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <errno.h>

int net_listen(const struct log_cfg *lcfg, int af, unsigned short port) {
	int ret, sockfd;
	union {
		struct sockaddr_in addr4;
		struct sockaddr_in6 addr6;
	} a;
	if (af != AF_INET && af != AF_INET6) {
		log_err(lcfg, "net: Unsupported address family!");
		return -1;
	}
	/* zero-initialize union */
	memset(&a, 0, sizeof(a));
	/* use any interface */
	if (af == AF_INET) {
		a.addr4.sin_family = af;
		a.addr4.sin_addr.s_addr = INADDR_ANY;
	} else if (af == AF_INET6) {
		a.addr6.sin6_family = af;
		a.addr6.sin6_addr = in6addr_any;
	}
	errno = 0;
	if ((sockfd = socket(af, SOCK_STREAM, 0)) == -1) {
		log_perror(lcfg, errno, "net: socket");
		return -1;
	}
#ifdef IPV6_V6ONLY
	/* disable IPv4 support with IPv6 sockets */
	if (af == AF_INET6) {
		int flag = 1;
		setsockopt(
			sockfd,
			IPPROTO_IPV6,
			IPV6_V6ONLY,
			(void *) &flag,
			sizeof(flag)
		);
	}
#endif
	/* set the port and bind */
	if (af == AF_INET) {
		a.addr4.sin_port = htons(port);
	} else if (af == AF_INET6) {
		a.addr6.sin6_port = htons(port);
	}
	errno = 0;
	ret = bind(
		sockfd,
		(af == AF_INET) ?
			(struct sockaddr*) &(a.addr4) :
			(struct sockaddr*) &(a.addr6),
		(af == AF_INET) ?
			(socklen_t) sizeof(a.addr4) :
			(socklen_t) sizeof(a.addr6)
	);
	if (ret == -1) {
		log_perror(lcfg, errno, "net: bind");
		return -1;
	}
	/* have at most 8 items in the backlog */
	errno = 0;
	ret = listen(sockfd, 8);
	if (ret == -1) {
		log_perror(lcfg, errno, "net: listen");
		return -1;
	}
	return sockfd;
}

int net_accept(
	const struct log_cfg *lcfg,
	int sockfd,
	int af,
	struct sockaddr *storeaddr
) {
	int ret = -1, storerr = errno;
	socklen_t addr_size = (
		(af == AF_INET) ?
			(socklen_t) sizeof(struct sockaddr_in) :
			(socklen_t) sizeof(struct sockaddr_in6)
	);
	if (af != AF_INET && af != AF_INET6) {
		log_err(lcfg, "net: Unsupported address family!");
		return -1;
	}
	memset(storeaddr, 0, addr_size);
	errno = 0;
	ret = accept(
		sockfd,
		storeaddr,
		&addr_size
	);
	if (ret == -1) {
		storerr = errno;
		log_perror(lcfg, storerr, "net: accept");
	}
	errno = storerr;
	return ret;
}

/* vi: set sts=8 ts=8 sw=8 noexpandtab: */
