#include "log.h"
#include "request.h"
#include <time.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <unistd.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <arpa/inet.h>

/* Reads a line from stream f, and stores it in buf.
 * In case it is longer than len, it is truncated to
 * len - 1 bytes, and the number of bytes (excluding
 * the terminating NULL byte) is returned. In case of
 * an error, if any bytes were read, the number of bytes
 * read will be returned, and errno will stay the same.
 * If no bytes were read, errno is set accordingly.
 *
 * The buffer is guaranteed to be NULL-terminated but
 * may contain embedded NULL bytes.
 *
 * On error, -1 is returned.
 */
int request_getline(char *buf, int len, FILE *f) {
	int ret = 0, storerr = errno, b = 0;

	if (buf == NULL || len <= 0 || f == NULL) {
		errno = EINVAL;
		return -1;
	}

	flockfile(f);

	errno = 0;
	while (ret < len - 1 && (b = getc_unlocked(f)) != EOF) {
		buf[ret] = (char) b;
		ret += 1;
		if (b == (int) '\n') {
			break;
		}
		errno = 0;
	}
	if (b == EOF && ret == 0 && ferror(f)) {
		storerr = EIO;
		ret = -1;
	} else {
		buf[ret] = '\0';
	}

	funlockfile(f);

	errno = storerr;
	return ret;
}

/* Decodes a URI string buf of len bytes (excluding the
 * terminating NULL byte) in-place. Since URI-encoded
 * characters take three times the space of normal
 * characters, this should not be an issue. Returns the
 * number of decoded bytes that wound up in the buffer,
 * excluding the terminating NULL byte.
 *
 * The buffer is guaranteed to be NULL-terminated but
 * may contain embedded NULL bytes.
 *
 * On error, -1 is returned.
 */
int request_decodeuri(char *buf, int len) {
	int ri, wi, di;
	char decode = '\0';
	if (buf == NULL || len <= 0) {
		errno = EINVAL;
		return -1;
	}
	for (ri = 0, wi = 0, di = 0; ri < len && wi < len; ri += 1) {
		if (di == 0) {
			/* start decoding */
			if (buf[ri] == '%') {
				decode = '\0';
				di += 1;
				continue;
			}
			/* normal write */
			buf[wi] = buf[ri];
			wi += 1;
			continue;
		} else if (di == 1 || di == 2) {
			char off = '\0';
			if (isxdigit(buf[ri]) == 0) {
				/* not a hexadecimal */
				int sri;
				for (sri = ri - di; sri <= ri; sri += 1) {
					buf[wi] = buf[sri];
					wi += 1;
				}
				di = 0;
				continue;
			}
			/* itsy bitsy magicsy */
			if (buf[ri] >= '0' && buf[ri] <= '9') {
				off = 0 - '0';
			} else if (buf[ri] >= 'a' && buf[ri] <= 'f') {
				off = 10 - 'a';
			} else if (buf[ri] >= 'A' && buf[ri] <= 'F') {
				off = 10 - 'A';
			}
			decode |= (buf[ri] + off) << (2 - di) * 4;
			if (di == 2) {
				buf[wi] = decode;
				wi += 1;
				di = 0;
			} else {
				di += 1;
			}
			continue;
		}
	}
	buf[wi] = '\0';
	return wi;
}

/* Rewrites the requested path and sets the response code to 400
 * and returns -1 if the path looks dreadful. Returns 0 on redirect,
 * 1 on HTML, 2 on text.
 */
int request_rewrite(struct request_ent *rent) {
	size_t readsize = strlen(rent->path);
	/* <path> + i/ + fff/ + '\0' - initial '/' */
	size_t bufsize = readsize + 2 + 4;
	char *buf;
	unsigned int ri, wi, di, fail = 0;
	if (strncmp(rent->path, "/", 2) == 0) {
		free(rent->path);
		rent->path = strdup("index.html");
		return 1;
	} else if (strncmp(rent->path, "/robots.txt", 12) == 0) {
		free(rent->path);
		rent->path = strdup("robots.txt");
		return 2;
	}
	/* minimum tiny length is '/' + 4 chars (base dir: 3; url: 1)
	 * this is two characters longer with e/ urls
	 */
	if (
		readsize < 5 ||
		(
			(rent->path[1] == 'e' && rent->path[2] == '/') &&
			readsize < 7
		)
	) {
		rent->code = 400;
		rent->path[0] = '\0';
		return -1;
	}
	buf = malloc(bufsize);
	if (buf == NULL) {
		rent->code = 500;
		rent->path[0] = '\0';
		return -1;
	}
	wi = 0;
	ri = 1;
	/* handle all the allowed slashes */
	if (rent->path[1] == 'e' && rent->path[2] == '/') {
		buf[wi + 0] = rent->path[ri + 0];
		buf[wi + 1] = rent->path[ri + 1];
		wi += 2;
		ri += 2;
	} else {
		buf[wi + 0] = 'i';
		buf[wi + 1] = '/';
		wi += 2;
	}
	/* do the rest of the rewrite */
	di = 0;
	while (ri < readsize) {
		if (rent->path[ri] == '/' || rent->path[ri] == '\\') {
			fail = 1;
			break;
		}
		buf[wi] = rent->path[ri];
		/* base directory, fff/ */
		if (di < 4) {
			di += 1;
			if (di == 3) {
				wi += 1;
				buf[wi] = '/';
				ri -= 3;
			}
		}
		wi += 1;
		ri += 1;
	}
	/* terminate */
	buf[wi] = '\0';
	if (fail == 1) {
		free(buf);
		rent->code = 400;
		rent->path[0] = '\0';
		return -1;
	}
	free(rent->path);
	rent->path = buf;
	return 0;
}

#define RESPCASE(x, s) \
	case x: return s

const char *request_get_respstr(
	int code
) {
	switch (code) {
	RESPCASE(200, "OK");
	RESPCASE(302, "Found");
	RESPCASE(400, "Bad Request");
	RESPCASE(403, "Forbidden");
	RESPCASE(404, "Not Found");
	RESPCASE(405, "Method Not Allowed");
	RESPCASE(408, "Request Timeout");
	RESPCASE(413, "Request Entity Too Large");
	RESPCASE(418, "I'm a teapot");
	RESPCASE(431, "Request Header Fields Too Large");
	RESPCASE(500, "Internal Server Error");
	RESPCASE(501, "Not Implemented");
	RESPCASE(505, "HTTP Version Not Supported");
	default:
		return "Unknown Response Code";
	}
}

const char *request_get_color(
	int code
) {
	int base = code - (code % 100);
	switch (base) {
	/* green */
	case 200:
	case 300:
		return "32";
	/* red */
	case 500:
		return "31";
	/* brown */
	case 400:
	default:
		return "33";
	}
}

int request_log(
	const struct log_cfg *lcfg,
	const struct request_ent *rent
) {
	/* 3 + NULL */
	char respcodebuf[4];
	/* brackets around IPv6 */
	char ipbuf[INET6_ADDRSTRLEN + 2];
	int pi, plen = (rent->path != NULL) ? strlen(rent->path) : 0;
	unsigned short port = 0;
	snprintf(respcodebuf, sizeof(respcodebuf), "%d", rent->code);
	/* nullify control characters */
	for (pi = 0; pi < plen; pi += 1) {
		if (rent->path[pi] < 32) {
			rent->path[pi] = '\0';
		}
	}
	if (rent->ip != NULL) {
		const char *ipret = NULL;
		int storerr = 0;
		errno = 0;
		if (rent->ip->sa_family == AF_INET) {
			ipret = inet_ntop(
				AF_INET,
				(void *) &(
					((const struct sockaddr_in*) rent->ip)
					->sin_addr
				),
				ipbuf,
				INET_ADDRSTRLEN
			);
			storerr = errno;
			port = htons(
				((const struct sockaddr_in*) rent->ip)->sin_port
			);
		} else if (rent->ip->sa_family == AF_INET6) {
			/* starting bracket */
			ipbuf[0] = '[';
			/* write at an offset */
			ipret = inet_ntop(
				AF_INET6,
				(void *) &(
					((const struct sockaddr_in6*) rent->ip)
					->sin6_addr
				),
				&(ipbuf[1]),
				INET6_ADDRSTRLEN
			);
			storerr = errno;
			/* closing bracket */
			if (ipret != NULL) {
				strncat(ipbuf, "]", 1);
			}
			port = htons(
				((const struct sockaddr_in6*) rent->ip)
				->sin6_port
			);
		}
		if (ipret == NULL) {
			log_perror(
				lcfg,
				storerr,
				"request: inet_ntop"
			);
			ipbuf[0] = '\0';
		}
	}
	return log_raw(
		lcfg,
		"%s:%hu - \"%s\" - %s - W %.3fms - R %.3fms",
		respcodebuf,
		request_get_color(rent->code),
		ipbuf,
		port,
		rent->raw_request,
		rent->ua,
		rent->wait * (double) 1000.0,
		rent->dt * (double) 1000.0
	);
}

/* shove common headers, parsed from rent */
void request_put_common(
	const struct log_cfg *lcfg,
	const struct request_ent *rent
) {
	int ret;
	/* HTTP protocol line */
	errno = 0;
	ret = fprintf(
		rent->sock,
		"HTTP/%d.%d %d %s\r\n",
		rent->v_major,
		rent->v_minor,
		rent->code,
		request_get_respstr(rent->code)
	);
	if (ret == -1) {
		log_perror(
			lcfg,
			errno,
			"request: fprintf"
		);
		return;
	}
	/* Server header */
	fputs("Server: mek.lu\r\n", rent->sock);
	/* Date header */
	{
		const char *dformat = "%a, %d %b %Y %H:%M:%S GMT";
		char datebuf[64];
		struct timespec tp;
		struct tm t;
		datebuf[0] = '\0';
		clock_gettime(CLOCK_REALTIME, &tp);
		gmtime_r(&(tp.tv_sec), &t);
		strftime(datebuf, sizeof(datebuf), dformat, &t);
		if (datebuf[0] != '\0') {
			fprintf(
				rent->sock,
				"%s: %s\r\n",
				"Date",
				datebuf
			);
		}
	}
}

static const char *request_error_fmt =
	"<!DOCTYPE html>\n"
	"<html xmlns=\"http://www.w3.org/1999/xhtml\">\n"
	"<head>\n"
	"<meta charset=\"utf-8\" />\n"
	"<title>%d %s</title>\n"
	"</head>\n"
	"<body>\n"
	"<h1>%d %s</h1>\n"
	"<p>Your request could not be served.</p>\n"
	"</body>\n"
	"</html>\n"
;

int request_get_error_body_length(
	const struct log_cfg *lcfg,
	const struct request_ent *rent
) {
	const char *respstr = request_get_respstr(rent->code);
	int ret;
	errno = 0;
	ret = snprintf(
		NULL,
		0,
		request_error_fmt,
		rent->code, respstr,
		rent->code, respstr
	);
	if (ret == -1) {
		log_perror(
			lcfg,
			errno,
			"request: errorlen: snprintf"
		);
	}
	return ret;
}

void request_put_error_body(
	const struct log_cfg *lcfg,
	const struct request_ent *rent
) {
	const char *respstr = request_get_respstr(rent->code);
	int ret;
	errno = 0;
	ret = fprintf(
		rent->sock,
		request_error_fmt,
		rent->code, respstr,
		rent->code, respstr
	);
	if (ret == -1) {
		log_perror(
			lcfg,
			errno,
			"request: errorbody: fprintf"
		);
	}
}

#define REQUEST_MAX_HEADERS 100

int request_populate(struct request_ent *rent) {
	int ret = 0, line = 0, lineret;
	char buf[4096];
	for (line = 0; line < REQUEST_MAX_HEADERS; line += 1) {
		unsigned int off = 0, llen = 0;
		while (
			(lineret = request_getline(
				&(buf[off]),
				sizeof(buf) - off,
				rent->sock
			)) > 0 &&
			off < sizeof(buf)
		) {
			off += lineret;
			if (buf[off - 1] == '\n') {
				/* that's enough */
				break;
			}
		}
		/* read error */
		if (lineret == -1) {
			rent->code = 500;
			return -1;
		}
		/* line too long */
		if (off == sizeof(buf) && buf[off - 1] != '\n') {
			rent->code = 431;
			return 0;
		}
		llen = strlen(buf);
		if (
			/* line too short */
			llen < 2 ||
			/* embedded NULL bytes */
			llen != off ||
			/* must be properly terminated */
			buf[llen - 2] != '\r' ||
			buf[llen - 1] != '\n'
		) {
			rent->code = 400;
			return 0;
		}
		/* line terminators convolute processing */
		buf[llen - 2] = '\0';
		/* HTTP request line */
		if (line == 0) {
			char *lp, *lpl;
			int spaces = 0;
			rent->raw_request = strndup(buf, llen);
			lp = buf;
			do {
				lpl = lp;
				lp = strchr(lp, ' ');
				if (spaces == 0) {
					/* method */
					char *m = strndup(lpl, lp - lpl);
					if (
						strcmp(m, "GET") != 0 &&
						strcmp(m, "HEAD") != 0
					) {
						rent->code = 400;
						if (strcmp(m, "BREW") == 0) {
							rent->code = 418;
						}
						free(m);
						return 0;
					}
					rent->method = m;
				} else if (spaces == 1) {
					/* path */
					char *qp;
					int plen, dlen, pi;
					rent->path = strndup(lpl, lp - lpl);
					/* strip the query string */
					qp = strchr(rent->path, '?');
					if (qp != NULL) {
						qp[0] = '\0';
					}
					plen = strlen(rent->path);
					/* decode the path */
					dlen = request_decodeuri(rent->path, plen);
					/* nullify control characters */
					for (pi = 0; pi < dlen; pi += 1) {
						if (rent->path[pi] < 32) {
							rent->path[pi] = '\0';
						}
					}
					plen = strlen(rent->path);
					if (
						/* embedded NULL */
						dlen != plen ||
						/* must start with a slash
						 * TODO: treat URIs right
						 */
						rent->path[0] != '/'
					) {
						rent->code = 400;
						return 0;
					}
					/* get rid of control characters */
				} else if (spaces == 2) {
					/* HTTP version */
					int maj, min;
					if (
						sscanf(
							lpl,
							"HTTP/%d.%d",
							&maj,
							&min
						) != 2
					) {
						rent->code = 400;
						return 0;
					}
					if (maj <= 0 || min < 0) {
						rent->code = 400;
						return 0;
					} else if (!(
						/* only support 1.1 and 1.0 */
						(maj == 1 && min == 1) ||
						(maj == 1 && min == 0)
					)) {
						rent->code = 505;
						return 0;
					}
					rent->v_major = maj;
					rent->v_minor = min;
				}
				if (lp != NULL) {
					spaces += 1;
					lp += 1;
				}
			} while (lp != NULL);
			/* there should be two spaces on the line */
			if (spaces != 2) {
				rent->code = 400;
				return 0;
			}
		} else if (buf[0] == '\0') {
			/* end of headers */
			ret = 1;
			break;
		} else {
			/* headers */
			char *cp = strchr(buf, ':');
			if (cp == NULL || cp[1] != ' ') {
				rent->code = 400;
				return 0;
			}
			if (
				strncasecmp(
					"User-Agent: ",
					buf,
					/* don't compare the NULL byte */
					sizeof("User-Agent: ") - 1
				) == 0
			) {
				rent->ua = strdup(&(cp[2]));
			}
		}
	}
	return ret;
}

int request_process(
	const struct log_cfg *lcfg,
	int sockfd,
	double delay,
	const struct sockaddr *addr
) {
	int ret = EXIT_FAILURE;
	struct request_ent rent;
	for (;;) {
		struct timespec tp_b, tp_e;
		int rr = -1, fsize = 0;
		time_t fmodified = 0;
		/* a file to be read */
		FILE *f = NULL;
		clock_gettime(CLOCK_MONOTONIC, &tp_b);
		/* initialise the request */
		memset(&rent, 0, sizeof(rent));
		/* internal value to indicate not being set */
		rent.code = -1;
		rent.ip = addr;
		rent.wait = delay;
		rent.v_major = 1;
		rent.v_minor = 0;
		errno = 0;
		rent.sock = fdopen(dup(sockfd), "rb+");
		if (rent.sock == NULL) {
			log_perror(
				lcfg,
				errno,
				"request: fdopen"
			);
			goto quit;
		}
		/* populate the request entity */
		if (request_populate(&rent) == -1) {
			/* quit on read error */
			goto quit;
		}
		/* rewrite the path */
		rr = request_rewrite(&rent);
		if (rr >= 0) {
			errno = 0;
			f = fopen(rent.path, "rb");
			if (f == NULL) {
				if (errno == EACCES) {
					rent.code = 403;
				} else {
					rent.code = 404;
				}
				rr = -1;
			} else {
				struct stat s;
				if (fstat(fileno(f), &s) == 0) {
					fsize = s.st_size;
					fmodified = s.st_mtime;
				}
			}
		}
		if (rr == 0) {
			rent.code = 302;
			fsize = 0;
		} else if (rr == 1 || rr == 2) {
			if (rent.code == -1) {
				rent.code = 200;
			}
		}
		/* if we don't have a response code yet, 500 */
		if (rent.code == -1) {
			rent.code = 500;
		}
		/* put common headers */
		request_put_common(lcfg, &rent);
		/* put request-specific headers */
		if (rr == 0) {
			/* redirection */
			char buf[64];
			int ws = 0;
			fputs("Location: ", rent.sock);
			while (
				(ws = request_getline(
					buf, sizeof(buf), f
				)) > 0
			) {
				char die = 0;
				if (buf[ws - 1] == '\n') {
					die = 1;
					if (buf[ws - 2] == '\r') {
						ws -= 1;
					}
					ws -= 1;
				}
				if (ws > 0) {
					fwrite(buf, ws, 1, rent.sock);
				}
				if (die == 1) {
					break;
				}
			}
			fputs("\r\n", rent.sock);
		}
		if (rr >= 0 && rr <= 2) {
			/* modification date */
			const char *dformat = "%a, %d %b %Y %H:%M:%S GMT";
			char datebuf[64];
			struct tm t;
			datebuf[0] = '\0';
			gmtime_r(&(fmodified), &t);
			strftime(datebuf, sizeof(datebuf), dformat, &t);
			if (datebuf[0] != '\0') {
				fprintf(
					rent.sock,
					"%s: %s\r\n",
					"Last-Modified",
					datebuf
				);
			}
			/* content type and length */
			fprintf(
				rent.sock,
				"Content-Type: %s; charset=utf-8\r\n"
				"Content-Length: %d\r\n",
				(rr == 1) ?
					"application/xhtml+xml" :
					"text/plain",
				fsize
			);
		}
		/* error :( */
		if (rent.code >= 500) {
			fputs("Connection: close\r\n", rent.sock);
		} else if (rent.v_major == 1 && rent.v_minor == 0) {
			/* do explicit keepalives for HTTP/1.0 when no
			 * error has been encountered; implicit with HTTP/1.1
			 */
			fputs("Connection: keep-alive\r\n", rent.sock);
		}
		if (rent.code >= 400) {
			fprintf(
				rent.sock,
				"Content-Type: %s\r\n"
				"Content-Length: %d\r\n",
				"application/xhtml+xml; charset=utf-8",
				request_get_error_body_length(lcfg, &rent)
			);
		}
		/* close headers */
		fprintf(rent.sock, "\r\n");
		/* put body, if any
		 * e.g. /robots.txt, /, error pages
		 */
		if (strcmp(rent.method, "HEAD") != 0) {
			if (rent.code == 200) {
				char fbuf[64];
				size_t fret = 0;
				while (
					(fret = fread(
						fbuf,
						1,
						sizeof(fbuf),
						f
					)) > 0
				) {
					fwrite(fbuf, fret, 1, rent.sock);
				}
			}
			/* error :( */
			if (rent.code >= 400) {
				request_put_error_body(lcfg, &rent);
			}
		}
		/* flush sent data */
		fflush(rent.sock);
		/* calculate delta time */
		clock_gettime(CLOCK_MONOTONIC, &tp_e);
		rent.dt = (double) (
			(double) tp_e.tv_sec - (double) tp_b.tv_sec +
			(
				(double) tp_e.tv_nsec -
				(double) tp_b.tv_nsec
			) / (double) 1000000000.0
		);
		/* log it */
		request_log(lcfg, &rent);
		/* free the relevant fields */
#define FREEANDNULL(x) free(x); x = NULL
		FREEANDNULL(rent.method);
		FREEANDNULL(rent.path);
		FREEANDNULL(rent.ua);
		FREEANDNULL(rent.raw_request);
		/* close the file we opened */
		if (f != NULL) {
			fclose(f);
			f = NULL;
		}
		/* process next client request, or die */
		if (rent.code >= 500) {
			goto quit;
		} else {
			struct pollfd pfd;
			pfd.fd = sockfd;
			pfd.events = POLLIN | POLLHUP;
			pfd.revents = 0;
			/* 5 second keepalive timeout */
			if (
				poll(&pfd, 1, 5000) > 0 &&
				(pfd.revents & POLLHUP) != POLLHUP
			) {
				continue;
			} else {
				break;
			}
		}
	}
	ret = EXIT_SUCCESS;
quit:
	/* close the connection */
	if (rent.sock != NULL) {
		fclose(rent.sock);
	}
	errno = 0;
	shutdown(sockfd, SHUT_RDWR);
	log_perror(
		lcfg,
		errno,
		"request: shutdown"
	);
	return ret;
}