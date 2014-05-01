#ifndef __mekdotlu_log_h
#define __mekdotlu_log_h

#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>

struct log_cfg {
	/* log file path */
	char *file;
	int _fd;
};

int log_init(struct log_cfg *cfg);
int log_kill(struct log_cfg *cfg);

int log_raw(
	const struct log_cfg *cfg,
	const char *format,
	const char *prefix,
	const char *color,
	...
);

int log_err(const struct log_cfg *cfg, const char *format, ...);
int log_wrn(const struct log_cfg *cfg, const char *format, ...);
int log_ok(const struct log_cfg *cfg, const char *format, ...);
int log_reg(const struct log_cfg *cfg, const char *format, ...);

int log_perror(const struct log_cfg *cfg, int err, const char *prefix, ...);

#endif /* __mekdotlu_log_h */

/* vi: set sts=8 ts=8 sw=8 noexpandtab: */
