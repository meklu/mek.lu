#include "log.h"
#include "request.h"
#include "clock.h"
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <arpa/inet.h>

/* Reads a line from descriptor f, and stores it in buf.
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
int request_getline(char *buf, int len, int fd) {
	int ret = 0, storerr = errno, r = -1;
	char b = 0;

	if (buf == NULL || len <= 0 || fd < 0) {
		errno = EINVAL;
		return -1;
	}

	errno = 0;
	while (ret < len - 1 && (r = read(fd, &b, 1)) > 0) {
		buf[ret] = b;
		ret += 1;
		if (b == '\n') {
			break;
		}
		errno = 0;
	}
	if (r == -1 && ret == 0) {
		storerr = errno;
		ret = -1;
	} else {
		buf[ret] = '\0';
	}

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
			char isa = buf[ri] >= 'a' && buf[ri] <= 'f';
			char isA = buf[ri] >= 'A' && buf[ri] <= 'F';
			char isn = buf[ri] >= '0' && buf[ri] <= '9';
			if (!(isa || isA || isn)) {
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
			if (isn) {
				off = 0 - '0';
			} else if (isa) {
				off = 10 - 'a';
			} else if (isA) {
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

/* Returns 1 if the string is valid UTF-8, 0 otherwise */
int request_utf8validate(const char *buf) {
	uint32_t dec = 0x0;
	char di = 0, dc = 0;
	size_t i, slen = strlen(buf);
	for (i = 0; i < slen; i += 1) {
		if (di == 0) {
			if ((buf[i] & 0x80) == 0) {
#ifdef DEBUG_UTF8
				fprintf(stderr, "u8v: reg (%c)\n", buf[i]);
#endif
				continue;
			} else if ((buf[i] & 0xF8) == 0xF8) {
				/* disallow 11111xxx lead bytes */
#ifdef DEBUG_UTF8
				fprintf(
					stderr,
					"u8v: %#.2X: bad lead byte\n",
					(int) buf[i]
				);
#endif
				return 0;
			} else if ((buf[i] & 0xC0) == 0xC0) {
				/* good lead byte: count leading bits;
				 * this is the codepoint byte count */
				union {
					char buf;
					char i;
				} b;
				b.buf = buf[i] & 0xF0;
				dc = 0;
				while (b.buf & 0x80) {
					dc += 1;
					b.buf <<= 1;
				}
#ifdef DEBUG_UTF8
				fprintf(
					stderr,
					"u8v: dc %d\n",
					dc
				);
#endif
				dec = 0;
				for (b.i = 0; b.i < 8 - dc; b.i += 1) {
					dec |= (buf[i] & (1 << b.i)) <<
						((dc - 1) * 6);
				}
				di = 1;
			} else if ((buf[i] & 0xC0) == 0x80) {
#ifdef DEBUG_UTF8
				fprintf(stderr,
					"u8v: %#.2X: not a lead byte\n",
					(int) buf[i]
				);
#endif
				return 0;
			} else {
#ifdef DEBUG_UTF8
				/* ??? */
				fprintf(stderr,
					"u8v: %#.2X: ???\n",
					(int) buf[i]
				);
#endif
				return 0;
			}
		} else {
			if (di < dc) {
				if ((buf[i] & 0xC0) != 0x80) {
					/* if there are still decodable bytes
					 * left, they must be 10xxxxxx
					 */
#ifdef DEBUG_UTF8
					fprintf(
						stderr,
						"u8v: missing %d bytes\n",
						dc - di
					);
#endif
					return 0;
				}
				/* 0x3F == ~0xC0
				 *
				 * Without ridiculous casts 0xC0 will be
				 * handled as an int-wide value and thus
				 * its complement will be bogus.
				 */
				dec |= (buf[i] & 0x3F) << (6 * (dc - di - 1));
				di += 1;
			}
			if (di == dc) {
#ifdef DEBUG_UTF8
				fprintf(
					stderr,
					"u8v: d U+%X\n",
					dec
				);
#endif
				/* codepoints above 0x10FFFF are illegal
				 * (RFC 3629)
				 */
				if (dec > 0x10FFFF) {
					return 0;
				}
				/* so are the following: [0xD800, 0xDFFF] */
				if (dec >= 0xD800 && dec <= 0xDFFF) {
					return 0;
				}
				di = 0;
			}
		}
	}
	return 1;
}

/* Returns the byte length of len UTF-8 codepoints. If there
 * aren't enough bytes in the string, returns len. If there
 * aren't enough codepoints in the string, returns the minimum
 * safe number to advance the string by that is also larger than
 * or equal to len.
 */
size_t request_utf8cplen(const char *buf, size_t len) {
	size_t i, slen = strlen(buf), c;
#ifdef DEBUG_UTF8
	fprintf(stderr, "u8c: b %8p\n", buf);
	fprintf(stderr, "u8c: b %s\n", buf);
	fprintf(stderr, "u8c: l %zu\n", len);
#endif
	i = c = 0;
	while (i < slen && c < len) {
		c += 1;
		i += 1;
		while ((buf[i] & 0xC0) == 0x80 && i < slen) {
			i += 1;
		}
	}
#ifdef DEBUG_UTF8
	fprintf(stderr, "u8c: c %zu, i %zu\n", c, i);
	fputs("u8c: r ", stderr);
	fwrite(buf, 1, i, stderr);
	fputs("\n", stderr);
#endif
	if (len > i) {
		return len;
	}
	return i;
}

/* Rewrites the requested path and sets the response code to 400
 * and returns -1 if the path looks dreadful. Returns 0 on redirect,
 * 1 on HTML, 2 on text.
 */
int request_rewrite(struct request_ent *rent) {
	size_t readsize;
	/* get the base directory (/[ei]/%s) byte length */
	size_t u8prefix;
	/* <path> + i/ + fff/ + '\0' - initial '/' */
	size_t bufsize;
	char *buf;
	unsigned int ri, wi, di, fail = 0;
	if (rent->path == NULL) {
		return -1;
	}
	if (strncmp(rent->path, "/", 2) == 0) {
		free(rent->path);
		rent->path = strdup("index.html");
		return 1;
	} else if (strncmp(rent->path, "/robots.txt", 12) == 0) {
		free(rent->path);
		rent->path = strdup("robots.txt");
		return 2;
	}
	if ((readsize = strlen(rent->path)) == 0) {
		return -1;
	}
	/* validate size and encoding */
	if (
		readsize < 2 ||
		request_utf8validate(rent->path) == 0
	) {
		rent->code = 400;
		rent->path[0] = '\0';
		return -1;
	}
	/* get the base directory (/[ei]/%s) byte length */
	u8prefix = request_utf8cplen(
		&(rent->path[1 + ((
			rent->path[1] == 'e' &&
			rent->path[2] == '/'
		) ? 2 : 0)]),
		3
	);
	/* <path> + i/ + fff/ + '\0' - initial '/' */
	bufsize = readsize + 2 + u8prefix + 1;
	/* minimum tiny length is '/' + 3 bytes (base dir)
	 * this is two characters longer with e/ urls
	 */
	if (
		readsize < 1 + u8prefix ||
		(
			(rent->path[1] == 'e' && rent->path[2] == '/') &&
			readsize < 1 + u8prefix + 2
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
		if (di < u8prefix + 1) {
			di += 1;
			if (di == u8prefix) {
				wi += 1;
				buf[wi] = '/';
				ri -= di;
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
		if (rent->path[pi] >= 0 && rent->path[pi] < 32) {
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
	ret = dprintf(
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
			"request: dprintf"
		);
		return;
	}
	/* Server header */
	dprintf(rent->sock, "Server: mek.lu\r\n");
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
			dprintf(
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
	ret = dprintf(
		rent->sock,
		request_error_fmt,
		rent->code, respstr,
		rent->code, respstr
	);
	if (ret == -1) {
		log_perror(
			lcfg,
			errno,
			"request: errorbody: dprintf"
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
		/* nothing to see here */
		if (line == 0 && lineret <= 0) {
			rent->code = 0;
			return 0;
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
			char lok;
			int spaces = 0;
			rent->raw_request = strndup(buf, llen);
			lp = buf;
			do {
				lok = 1;
				lpl = lp;
				lp = strchr(lpl, ' ');
				if (lp == NULL) {
					lok = 0;
					lp = &(buf[llen - 2]);
				}
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
						if (
							rent->path[pi] >= 0 &&
							rent->path[pi] < 32
						) {
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
				if (lok == 1) {
					spaces += 1;
					lp += 1;
				}
			} while (lok == 1);
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
	struct pollfd pfd;
	pfd.fd = sockfd;
	pfd.events = POLLIN | POLLHUP;
	pfd.revents = 0;
	/* initial one-second timeout */
	if (poll(&pfd, 1, 1000) > 0) {
		/* quit on hangup */
		if ((pfd.revents & POLLHUP) == POLLHUP) {
			goto quit;
		}
	} else {
		/* poll timeout */
		goto quit;
	}
	/* all is okay */
	for (;;) {
		struct timespec tp_b, tp_e;
		int rr = -1, fsize = 0;
		time_t fmodified = 0;
		/* a file to be read */
		int f = -1;
		/* a lock for the file */
		struct flock fl;
		clock_gettime(CLOCK_MONOTONIC, &tp_b);
		/* initialise the request */
		memset(&rent, 0, sizeof(rent));
		rent.sock = sockfd;
		/* internal value to indicate not being set */
		rent.code = -1;
		rent.ip = addr;
		rent.wait = delay;
		rent.v_major = 1;
		rent.v_minor = 0;
		errno = 0;
		/* populate the request entity */
		rr = request_populate(&rent);
		if (rr == -1) {
			/* quit on read error */
			goto quit;
		} else if (rr == 0 && rent.code == 0) {
			/* the client disconnected */
			goto quit;
		} else if (rr > 0) {
			/* rewrite the path if the request was well-formed */
			rr = request_rewrite(&rent);
		} else {
			/* reset rr to -1: this signifies that an error
			 * page will be emitted further down the line,
			 * rather than a document or redirection
			 */
			rr = -1;
		}
		if (rr >= 0) {
			errno = 0;
			f = open(rent.path, O_RDONLY);
			if (f == -1) {
				if (errno == EACCES) {
					rent.code = 403;
				} else {
					rent.code = 404;
				}
				rr = -1;
			} else {
				struct stat s;
				/* initialize the lock */
				fl.l_type = F_RDLCK;
				fl.l_whence = SEEK_END;
				fl.l_start = 0;
				fl.l_len = 0;
				if (fcntl(f, F_SETLKW, &fl) == -1) {
					log_perror(
						lcfg,
						errno,
						"request: fcntl"
					);
				}
				/* stat the file */
				if (fstat(f, &s) == 0) {
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
		/* determine whether or not we want to kill the connection */
		if (
			/* server error */
			rent.code >= 500 ||
			/* malformed request, there may still be bytes on the
			 * pipe, which we do not appreciate */
			rent.code == 400 ||
			/* a teapot cannot make coffee, give up */
			rent.code == 418
		) {
			rent.kill = 1;
		}
		/* put common headers */
		request_put_common(lcfg, &rent);
		/* put request-specific headers */
		if (rr == 0) {
			/* redirection */
			char buf[64];
			int ws = 0;
			dprintf(sockfd, "Location: ");
			while (
				(ws = request_getline(
					buf, sizeof(buf), f
				)) > 0
			) {
				char die = 0;
				if (buf[ws - 1] == '\n') {
					die = 1;
					if (ws > 1 && buf[ws - 2] == '\r') {
						ws -= 1;
					}
					ws -= 1;
				}
				if (ws > 0) {
					errno = 0;
					if (write(sockfd, buf, ws) == -1) {
						log_perror(
							lcfg,
							errno,
							"request: write"
						);
					}
				}
				if (die == 1) {
					break;
				}
			}
			dprintf(sockfd, "\r\n");
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
				dprintf(
					rent.sock,
					"%s: %s\r\n",
					"Last-Modified",
					datebuf
				);
			}
			/* content type and length */
			dprintf(
				sockfd,
				"Content-Type: %s; charset=utf-8\r\n"
				"Content-Length: %d\r\n",
				(rr == 1) ?
					"application/xhtml+xml" :
					"text/plain",
				fsize
			);
		}
		/* error :( */
		if (rent.kill) {
			dprintf(sockfd, "Connection: close\r\n");
		} else if (rent.v_major == 1 && rent.v_minor == 0) {
			/* do explicit keepalives for HTTP/1.0 when no
			 * error has been encountered; implicit with HTTP/1.1
			 */
			dprintf(sockfd, "Connection: keep-alive\r\n");
		}
		if (rent.code >= 400) {
			dprintf(
				sockfd,
				"Content-Type: %s\r\n"
				"Content-Length: %d\r\n",
				"application/xhtml+xml; charset=utf-8",
				request_get_error_body_length(lcfg, &rent)
			);
		}
		/* close headers */
		dprintf(rent.sock, "\r\n");
		/* put body, if any
		 * e.g. /robots.txt, /, error pages
		 */
		if (rent.method == NULL || strcmp(rent.method, "HEAD") != 0) {
			if (rent.code == 200) {
				char fbuf[64];
				size_t fret = 0;
				while (
					(fret = read(
						f,
						fbuf,
						sizeof(fbuf)
					)) > 0
				) {
					errno = 0;
					if (write(sockfd, fbuf, fret) == -1) {
						log_perror(
							lcfg,
							errno,
							"request: write"
						);
					}
				}
			}
			/* error :( */
			if (rent.code >= 400) {
				request_put_error_body(lcfg, &rent);
			}
		}
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
		if (f != -1) {
			if (close(f) == -1) {
				log_perror(lcfg, errno, "request: close");
			}
			f = -1;
		}
		/* process next client request, or die */
		if (rent.kill) {
			goto quit;
		} else {
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
	errno = 0;
	shutdown(sockfd, SHUT_RDWR);
	log_perror(
		lcfg,
		errno,
		"request: shutdown"
	);
	return ret;
}

/* vi: set sts=8 ts=8 sw=8 noexpandtab: */
