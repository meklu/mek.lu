#include "request.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

int main(void) {
	char buf[4096];
	int r;
	errno = 0;
	while ((r = request_getline(buf, sizeof(buf), STDIN_FILENO)) > 0) {
		struct request_ent rent;
		printf(
			"\033[36mresp(%d|%u):\033[0m %s",
			r, (unsigned int) strlen(buf),
			buf
		);
		errno = 0;
		r = request_decodeuri(buf, r);
		printf(
			"\033[36mdeco(%d|%u):\033[0m %s",
			r, (unsigned int) strlen(buf),
			buf
		);
		errno = 0;
		memset(&rent, 0, sizeof(rent));
		rent.path = strndup(buf, strlen(buf) - 1);
		r = request_rewrite(&rent);
		printf(
			"\033[36mrewr(%d|%u):\033[0m %s\n",
			r, (unsigned int) strlen(rent.path),
			rent.path
		);
		free(rent.path);
		errno = 0;
	}
	if (r == 0) {
		fputs("\033[36mEOF\033[0m\n", stdout);
	} else {
		fputs("request_getline: ", stdout);
		fputs(strerror(errno), stdout);
		fputs("\n", stdout);
	}
	return 0;
}

/* vi: set sts=8 ts=8 sw=8 noexpandtab: */
