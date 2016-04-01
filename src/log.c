#include "log.h"
#include "clock.h"
#include <unistd.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

int vlog_raw(
	const struct log_cfg *cfg,
	const char *format,
	const char *prefix,
	const char *color,
	va_list vl
) {
	int ret = 0;
	struct flock lock;
	const char *tformat = "%Y-%m-%d %H:%M:%S %z";
	char tbuf[64];
	struct tm t;
	struct timespec ts;
	va_list vl_sec;

	va_copy(vl_sec, vl);

	clock_gettime(CLOCK_REALTIME, &ts);
	localtime_r(&ts.tv_sec, &t);
	strftime(tbuf, 64, tformat, &t);

	lock.l_whence = SEEK_END;
	lock.l_start = 0;
	lock.l_len = 0;

#define RETMINUS(expr) \
	if (expr == -1) { \
		va_end(vl_sec); \
		return -1; \
	}

#define TMPRET(t, r, expr) \
	t = expr; \
	r += t; \
	RETMINUS(t)

	/* log to file */
	if (cfg->_fd != -1) {
		int tmp;
		/* wait for lock */
		lock.l_type = F_WRLCK;
		if (fcntl(cfg->_fd, F_SETLKW, &lock) == -1) {
			perror("log: fcntl");
		}
		lseek(cfg->_fd, 0, SEEK_END);
		TMPRET(tmp, ret, dprintf(
			cfg->_fd,
			"[%s] ",
			tbuf
		));
		if (prefix != NULL) {
			TMPRET(tmp, ret, dprintf(
				cfg->_fd,
				"[%s] ",
				prefix
			));
		}
		TMPRET(tmp, ret, vdprintf(cfg->_fd, format, vl));
		TMPRET(tmp, ret, write(cfg->_fd, "\n", 1));
	}
	/* log to stdout */
	{
		unsigned char usecolor = (
			(cfg->forcecolor || isatty(STDOUT_FILENO)) &&
			color != NULL
		);
		/* wait for lock */
		lock.l_type = F_WRLCK;
		if (fcntl(STDOUT_FILENO, F_SETLKW, &lock) == -1) {
			perror("log: fcntl");
		}
		/* colours */
		if (usecolor) {
			RETMINUS(dprintf(STDOUT_FILENO, "\033[%sm", color));
		}
		RETMINUS(dprintf(
			STDOUT_FILENO,
			"[%s] ",
			tbuf
		));
		if (prefix != NULL) {
			RETMINUS(dprintf(
				STDOUT_FILENO,
				"[%s] ",
				prefix
			));
		}
		RETMINUS(vdprintf(STDOUT_FILENO, format, vl_sec));
		if (usecolor) {
			RETMINUS(dprintf(STDOUT_FILENO, "\033[0m"));
		}
		RETMINUS(write(STDOUT_FILENO, "\n", 1));
		/* release lock */
		lock.l_type = F_UNLCK;
		if (fcntl(STDOUT_FILENO, F_SETLK, &lock) == -1) {
			perror("log: fcntl");
		}
	}
	/* release the log file lock now; we want the line order to be
	 * consistent across the log file and standard output
	 */
	if (cfg->_fd != -1) {
		lock.l_type = F_UNLCK;
		if (fcntl(cfg->_fd, F_SETLK, &lock) == -1) {
			perror("log: fcntl");
		}
	}
	va_end(vl_sec);
	return ret;
#undef RETMINUS
}

int log_raw(
	const struct log_cfg *cfg,
	const char *format,
	const char *prefix,
	const char *color,
	...
) {
	va_list vl;
	int ret;

	va_start(vl, color);
	ret = vlog_raw(cfg, format, prefix, color, vl);
	va_end(vl);
	return ret;
}

int log_err(const struct log_cfg *cfg, const char *format, ...) {
	va_list vl;
	int ret;

	va_start(vl, format);
	ret = vlog_raw(cfg, format, "ERR", "31", vl);
	va_end(vl);
	return ret;
}

int log_wrn(const struct log_cfg *cfg, const char *format, ...) {
	va_list vl;
	int ret;

	va_start(vl, format);
	ret = vlog_raw(cfg, format, "WRN", "33", vl);
	va_end(vl);
	return ret;
}

int log_ok(const struct log_cfg *cfg, const char *format, ...) {
	va_list vl;
	int ret;

	va_start(vl, format);
	ret = vlog_raw(cfg, format, "OK", "32", vl);
	va_end(vl);
	return ret;
}

int log_reg(const struct log_cfg *cfg, const char *format, ...) {
	va_list vl;
	int ret;

	va_start(vl, format);
	ret = vlog_raw(cfg, format, NULL, "36", vl);
	va_end(vl);
	return ret;
}

int log_init(struct log_cfg *cfg) {
	int fd = -1, ret = 0;
	/* acquire a log file, if the config wants one */
	if (
		cfg->file != NULL &&
		cfg->file[0] != '\0'
	) {
		fd = open(cfg->file, O_WRONLY | O_CREAT, 0640);
		if (fd != -1) {
			lseek(fd, 0, SEEK_END);
			cfg->_fd = fd;
			ret = 1;
		} else {
			cfg->_fd = -1;
			log_wrn(
				cfg,
				"log: Could not open %s for writing",
				cfg->file
			);
			ret = 0;
		}
	} else {
		/* no need for a log file */
		cfg->_fd = -1;
		ret = 1;
	}
	return ret;
}

int log_kill(struct log_cfg *cfg) {
	int ret = 0;
	struct flock lock;
	if (cfg->_fd != -1) {
		/* wait for lock */
		lock.l_type = F_WRLCK;
		lock.l_whence = SEEK_END;
		lock.l_start = 0;
		lock.l_len = 0;
		fcntl(cfg->_fd, F_SETLKW, &lock);
		/* synchronize */
		fsync(cfg->_fd);
		if (close(cfg->_fd) == 0) {
			ret = 1;
		} else {
			ret = 0;
		}
		cfg->_fd = -1;
	} else {
		ret = 1;
	}
	return ret;
}

#define ERRLEN 256
#define FMTLEN 256
int log_perror(const struct log_cfg *cfg, int err, const char *prefix, ...) {
	va_list vl;
	int ret;
	char errbuf[ERRLEN];
	char fmtbuf[FMTLEN];
	if (err == 0) {
		return 0;
	}
	errbuf[0] = '\0';
	fmtbuf[0] = '\0';
	if (strerror_r(err, errbuf, ERRLEN) != 0) {
		snprintf(errbuf, sizeof(errbuf), "fail");
	}
	snprintf(fmtbuf, sizeof(fmtbuf), "%s: %s", prefix, errbuf);
	va_start(vl, prefix);
	ret = vlog_raw(cfg, fmtbuf, "ERR", "31", vl);
	va_end(vl);
	return ret;
}

/* vi: set sts=8 ts=8 sw=8 noexpandtab: */
