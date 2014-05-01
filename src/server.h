#ifndef __mekdotlu_server_h
#define __mekdotlu_server_h

#include "log.h"
#include <unistd.h>

struct server_cfg {
	char *root;
	char should_setuid;
	uid_t uid;
	gid_t gid;
	unsigned short port;
	/* we'll try to bind to both AF's on INADDR_ANY */
	int _sock;
	int _sock6;
	struct log_cfg _lcfg;
};

int server_init(struct server_cfg *cfg);
int server_kill(struct server_cfg *cfg);

void server_loop(const struct server_cfg *cfg);

int server_constrain(const struct server_cfg *cfg);

#endif /* __mekdotlu_server_h */

/* vi: set sts=8 ts=8 sw=8 noexpandtab: */
