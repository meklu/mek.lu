/* bootstraps the server
 * pretty much just a config reader
 */
#include "log.h"
#include "net.h"
#include "server.h"
#include <string.h>
#include <limits.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>
#include <pwd.h>

/* Required for OSX. */
#ifndef MAP_ANONYMOUS
#	define MAP_ANONYMOUS MAP_ANON
#endif

#define p(x) fputs(x "\n", f)
void print_usage(FILE *f) {
	p("USAGE:  mekdotlu <args>");
	p("        Arguments must be tightly specified. Use `-tvalue'");
	p("        instead of `-t value' or `-t=value'. If an argument is");
	p("        specified more than once, the last value for it will");
	p("        override the others, and no warning will be emitted.");
	p("");
	p("OPTIONS:");
	p("        -f      Follow symbolic links for paths specified");
	p("                on the command line.");
	p("        -u<str> Set user to switch to after listening on port.");
	p("        -p<num> Set listen port. Defaults to 8081.");
	p("        -r<str> Set document root. Default is current directory.");
	p("        -o<str> Set log file. Can be left blank to not log to a");
	p("                file. Default is ./mekdotlu.log");
	p("        -C      Force colored standard output.");
	p("");
	p("  (-h)  --help  Show this help and exit.");
	p("");
	p("EXAMPLE:");
	p("        Run the service on port 80, follow path symlinks and");
	p("        set the document root to `./urls'.");
	p("        $ mekdotlu -p80 -f -r./urls");
}
#undef p

/* return value needs to be freed */
char *config_realpath(const char *path, int symlinks) {
	const char *tmppath = (path != NULL) ? path : ".";
	int storerr = errno;
	struct stat buf;
	char *rp;
	if (lstat(tmppath, &buf) == -1) {
		return NULL;
	}
	if (symlinks == 0 && S_ISLNK(buf.st_mode)) {
		return NULL;
	}
	errno = 0;
	rp = realpath(tmppath, NULL);
	if (errno == 0) {
		errno = storerr;
	}
	return rp;
}

void populate_cfg(
	struct server_cfg *cfg,
	int argc,
	char **argv
) {
	int i, err, symlinks, help;
	unsigned short port = 0;
	struct {
		char *root;
		char *log;
	} f;

	char should_setuid = 0;
	struct {
		uid_t uid;
		gid_t gid;
	} setuid_info;

	memset(cfg, 0, sizeof(*cfg));
	memset(&f, 0, sizeof(f));
	symlinks = 0;
	help = 0;
	err = 0;
	/* defaults */
	cfg->_lcfg.file = strdup("mekdotlu.log");
	cfg->_lcfg.forcecolor = 0;
	cfg->root = config_realpath(NULL, 0);
	cfg->port = 8081;
	/* parse args */
	/* look for errors and -f first, and store path indices */
	for (i = 1; i < argc; i += 1) {
		if (argv[i][0] != '-') {
			/* malformed command line */
			fprintf(
				stderr,
				"Invalid argument [%d]: %s\n",
				i,
				argv[i]
			);
			err = 1;
			continue;
		}
#define NOVAL(x) \
		if (argv[i][2] != '\0') { \
			fprintf( \
				stderr, \
				"The -%c switch accepts no value\n", \
				x \
			); \
			err = 1; \
		}

		if (argv[i][1] == 'f') {
			NOVAL('f');
			symlinks = 1;
		} else if (argv[i][1] == 'C') {
			NOVAL('C');
			cfg->_lcfg.forcecolor = 1;
#undef NOVAL
		} else if (
			argv[i][1] == 'h' ||
			(
				argv[i][1] == '-' &&
				argv[i][2] == 'h' &&
				argv[i][3] == 'e' &&
				argv[i][4] == 'l' &&
				argv[i][5] == 'p'
			)

		) {
			help = 1;
		} else if (argv[i][1] == 'r') {
			f.root = &(argv[i][2]);
		} else if (argv[i][1] == 'o') {
			f.log = &(argv[i][2]);
		} else if (argv[i][1] == 'u') {
			char *buffer = NULL;
			struct passwd pwd, *result = NULL;
			int bufsize = sysconf(_SC_GETPW_R_SIZE_MAX);
			char *username = &(argv[i][2]);

			if (bufsize == -1) {
				fprintf(
					stderr,
					"Unable to prepare for setuid().\n"
				);
				err = 1;
				continue;
			}

			buffer = malloc(bufsize);
			if (
				getpwnam_r(
					username,
					&pwd,
					buffer,
					bufsize,
					&result
				) != 0
			) {
				fprintf(
					stderr,
					"Unable to perform user lookup for "
					"-u switch!\n"
				);
				err = 1;
				continue;
			}
			if (result == NULL) {
				fprintf(
					stderr,
					"The -u switch specified an invalid "
					"user '%s'.\n",
					username
				);
				err = 1;
				continue;
			}

			should_setuid = 1;
			setuid_info.uid = pwd.pw_uid;
			setuid_info.gid = pwd.pw_gid;
		} else if (argv[i][1] == 'p') {
			int scanret = sscanf(
				&(argv[i][2]),
				"%hu",
				&port
			);
			if (scanret != 1 || port == 0) {
				fprintf(
					stderr,
					"Could not parse port number: %s\n",
					&(argv[i][2])
				);
				err = 1;
			}
		} else {
			fprintf(
				stderr,
				"Unknown argument [%d]: %s\n",
				i,
				argv[i]
			);
			err = 1;
		}
	}
	if (err == 1 || help == 1) {
		if (err == 1) {
			fputs("\n", stderr);
			print_usage(stderr);
			exit(EXIT_FAILURE);
		} else {
			print_usage(stdout);
			exit(EXIT_SUCCESS);
		}
	}
	if (f.root != NULL) {
		free(cfg->root);
		cfg->root = config_realpath(f.root, symlinks);
	}
	if (f.log != NULL) {
		free(cfg->_lcfg.file);
		cfg->_lcfg.file = config_realpath(f.log, symlinks);
	}
	if (port != 0) {
		cfg->port = port;
	}
	if (should_setuid) {
		cfg->should_setuid = 1;
		cfg->uid = setuid_info.uid;
		cfg->gid = setuid_info.gid;
	}
}

int main(int argc, char **argv) {
	/* map the config in shared memory */
	struct server_cfg *cfg = mmap(
		NULL,
		sizeof(*cfg),
		PROT_READ | PROT_WRITE,
		MAP_SHARED | MAP_ANONYMOUS,
		-1,
		0
	);
	if (cfg == MAP_FAILED) {
		perror("main: mmap");
		return 1;
	}
	/* populate said config */
	populate_cfg(cfg, argc, argv);
	/* initialize it */
	if (log_init(&(cfg->_lcfg)) == 0) {
		log_wrn(
			&(cfg->_lcfg),
			"Everything you hold dear is dead."
		);
	}
	log_reg(&(cfg->_lcfg), "Initializing...");
	if (server_init(cfg) == 0) {
		return 1;
	}
	/* no need for these anymore */
	free(cfg->root);
	free(cfg->_lcfg.file);
	/* guard the config for the server lifetime duration */
	errno = 0;
	mprotect(cfg, sizeof(*cfg), PROT_READ);
	log_perror(&(cfg->_lcfg), errno, "main: mprotect");
	/* run the server */
	server_loop(cfg);
	log_reg(&(cfg->_lcfg), "Shutting down...");
	/* re-allow modifications */
	errno = 0;
	mprotect(cfg, sizeof(*cfg), PROT_READ | PROT_WRITE);
	log_perror(&(cfg->_lcfg), errno, "main: mprotect");
	/* kill the config */
	server_kill(cfg);
	log_kill(&(cfg->_lcfg));
	/* unmap it */
	errno = 0;
	if (munmap(cfg, sizeof(*cfg)) == -1) {
		log_perror(&(cfg->_lcfg), errno, "main: munmap");
	}
	/* done */
	return 0;
}

/* vi: set sts=8 ts=8 sw=8 noexpandtab: */
