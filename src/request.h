#ifndef __mekdotlu_request_h
#define __mekdotlu_request_h

#include "log.h"
#include <stdio.h>
#include <sys/socket.h>

struct request_ent {
	/* the socket's associated stream */
	FILE *sock;
	/* response code */
	int code;
	/* HTTP version to respond with */
	int v_major;
	int v_minor;
	/* how long this request took in seconds,
	 * written by request_process
	 */
	double dt;
	/* how long the parent took to call us */
	double wait;
	/* remote address */
	const struct sockaddr *ip;
	/* request method: GET/HEAD */
	char *method;
	/* requested path */
	char *path;
	/* client's user agent */
	char *ua;
	/* client's raw request */
	char *raw_request;
};

int request_getline(char *buf, int len, FILE *f);
int request_decodeuri(char *buf, int len);
int request_rewrite(struct request_ent *rent);

int request_process(
	const struct log_cfg *lcfg,
	int sockfd,
	double delay,
	const struct sockaddr *addr
);

#endif /* __mekdotlu_request_h */
