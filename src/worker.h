#ifndef __mekdotlu_worker_h
#define __mekdotlu_worker_h

#include "log.h"

void worker_loop(const struct log_cfg *lcfg, int ipcsock, int af, int sockfd);

#endif /* __mekdotlu_worker_h */

/* vi: set sts=8 ts=8 sw=8 noexpandtab: */
