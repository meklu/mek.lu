#ifndef __mekdotlu_net_h
#define __mekdotlu_net_h

#include "log.h"
#include <sys/socket.h>

int net_listen(const struct log_cfg *lcfg, int af, unsigned short port);
int net_accept(
	const struct log_cfg *lcfg,
	int sockfd,
	int af,
	struct sockaddr *storeaddr
);

#endif /* __mekdotlu_net_h */
