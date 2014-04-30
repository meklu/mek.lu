#include "server.h"
#include "worker.h"
#include "net.h"
#include "log.h"
#include <unistd.h>
#include <signal.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#ifdef __linux
#	include <sys/capability.h>
#	include <linux/capability.h>
#endif

/* attempt to chdir and chroot the process to the
 * document tree
 *
 * linux: reads and tries to drop the CAP_SYS_CHROOT
 * afterwards
 */
int server_constrain(const struct server_cfg *cfg) {
	const char *path = cfg->root;
#ifdef __linux
	/* get the process capabilities
	 *
	 * we are especially interested in CAP_SYS_CHROOT but
	 * we want to drop them all anyway, hence the cap_clear()
	 */
	cap_t caps = cap_get_proc();
	cap_flag_value_t cap_chroot = 0;

	cap_get_flag(
		caps,
		CAP_SYS_CHROOT,
		CAP_EFFECTIVE,
		&cap_chroot
	);
	cap_clear(caps);
#endif
	/* chroot needs an absolute pathname */
	if (path == NULL || path[0] != '/') {
		log_err(
			&(cfg->_lcfg),
			"server: Poor path name for document root: %s",
			path
		);
		return 0;
	}
	log_reg(&(cfg->_lcfg), "server: Setting document root to %s", path);
	errno = 0;
	if (chdir(path) == -1) {
		log_perror(&(cfg->_lcfg), errno, "server: chdir");
		return 0;
	}
	if (
		geteuid() == 0
#ifdef __linux
		/* check for the capability as well */
		|| cap_chroot == 1
#endif
	) {
		errno = 0;
		if (chroot(path) == -1) {
			log_perror(
				&(cfg->_lcfg),
				errno,
				"server: chroot"
			);
			return 0;
		} else {
			log_ok(
				&(cfg->_lcfg),
				"server: chroot successful!"
			);
		}
	} else {
		log_wrn(
			&(cfg->_lcfg),
			"server: No chroot done! Check permissions."
		);
#ifdef __linux
		log_wrn(
			&(cfg->_lcfg),
			"server: You may want to enable CAP_SYS_CHROOT:"
		);
		log_wrn(
			&(cfg->_lcfg),
			"server: # setcap cap_sys_chroot+ep <binary>"
		);
#endif
	}
	log_ok(
		&(cfg->_lcfg),
		"server: Document root set to %s",
		path
	);
#ifdef __linux
	/* drop the capabilities */
	errno = 0;
	if (cap_set_proc(caps) == -1) {
		log_perror(
			&(cfg->_lcfg),
			errno,
			"server: Error dropping capabilities"
		);
	} else {
		log_ok(
			&(cfg->_lcfg),
			"server: Dropped all capabilities"
		);
	}
	/* don't take a leak */
	cap_free(caps);
#endif
	return 1;
}

#define BINDADDR(_name, _addr, _af, _socket) \
	(_socket) = net_listen( \
		&(cfg->_lcfg), \
		(_af), \
		cfg->port \
	); \
	if ((_socket) == -1) { \
		log_err( \
			&(cfg->_lcfg), \
			"server: " _name ": Couldn't bind to " _addr ":%hu", \
			cfg->port \
		); \
	} else { \
		log_ok( \
			&(cfg->_lcfg), \
			"server: " _name ": Bound to " _addr ":%hu", \
			cfg->port \
		); \
	}

int server_init(struct server_cfg *cfg) {
	/* IPv4 */
	BINDADDR("ipv4", "0.0.0.0", AF_INET, cfg->_sock);
	/* IPv6 */
	BINDADDR("ipv6", "[::]", AF_INET6, cfg->_sock6);
	/* nothing was bound, abort, abort */
	if (cfg->_sock == -1 && cfg->_sock6 == -1) {
		return 0;
	}
	/* try to chroot */
	return server_constrain(cfg);
}

int server_kill(struct server_cfg *cfg) {
	if (cfg->_sock != -1) {
		close(cfg->_sock);
		cfg->_sock = -1;
	}
	if (cfg->_sock6 != -1) {
		close(cfg->_sock6);
		cfg->_sock6 = -1;
	}
	return 1;
}

#define FORKWORKER(_name, _socket, _closesocket, _wrkstate, _af) do { \
	if ( \
		(_wrkstate).respawn == 1 && \
		(_wrkstate).pid == -1 && \
		(_socket) != -1 \
	) { \
		log_reg( \
			&(cfg->_lcfg), \
			"server: Forking %s worker...", \
			(_name) \
		); \
		errno = 0; \
		/* make an IPC socket happen */ \
		if ( \
			socketpair( \
				AF_UNIX, \
				SOCK_STREAM, \
				0, \
				(_wrkstate).sock \
			) == -1 \
		) { \
			log_perror( \
				&(cfg->_lcfg), \
				errno, \
				"server: socketpair" \
			); \
			break; \
		} \
		errno = 0; \
		(_wrkstate).pid = fork(); \
		if ((_wrkstate).pid == 0) { \
			/* get rid of our signal handlers */ \
			sa.sa_handler = SIG_IGN; \
			sigemptyset(&(sa.sa_mask)); \
			REGSIG(SIGINT, &sa); \
			sa.sa_handler = SIG_DFL; \
			REGSIG(SIGTERM, &sa); \
			REGSIG(SIGQUIT, &sa); \
			/* we don't need the other worker's socket */ \
			if (_closesocket != -1) { \
				close(_closesocket); \
			} \
			/* close the parent's IPC socket */ \
			close((_wrkstate).sock[0]); \
			/* workers need not access to stdin */ \
			fclose(stdin); \
			worker_loop( \
				&(cfg->_lcfg), \
				(_wrkstate).sock[1], \
				(_af), \
				(_socket) \
			); \
			exit(EXIT_FAILURE); \
		} else if ((_wrkstate).pid == -1) { \
			log_perror( \
				&(cfg->_lcfg), \
				errno, \
				"server: Failed to fork " _name " worker" \
			); \
			/* clean up our socket */ \
			close((_wrkstate).sock[0]); \
		} \
		/* close the child's IPC socket */ \
		close((_wrkstate).sock[1]); \
	} } while (0)

#define IPCSEND(_wrkstate, _msg) \
	if ((_wrkstate).sock[0] != -1 && (_wrkstate).pid != -1) { \
		if (write((_wrkstate).sock[0], _msg, sizeof(_msg)) == -1) { \
			log_perror( \
				&(cfg->_lcfg), \
				errno, \
				"server: ipc: %s", \
				# _wrkstate \
			); \
		} \
	}

/* a thing that server_loop shall be checking */
volatile sig_atomic_t server_run;

/* a signal handler for SIG{INT,TERM,QUIT} */
void server_quit_handler(int sig) {
	int old_errno = errno;
	fprintf(
		stderr,
		"Caught SIG%s, shutting down...\n",
		(sig == SIGINT) ? "INT" :
		(sig == SIGTERM) ? "TERM" :
		(sig == SIGQUIT) ? "QUIT" :
		"UNK"
	);
	server_run = 0;
	errno = old_errno;
}

#define REGSIG(_sig, _cfg) \
	if (sigaction(_sig, _cfg, NULL) == -1) { \
		log_perror( \
			&(cfg->_lcfg), \
			errno, \
			"server: sigaction (%s)", \
			# _sig \
		); \
		return; \
	}

void server_loop(const struct server_cfg *cfg) {
	struct worker_state {
		/* worker pid */
		pid_t pid;
		/* whether the worker should respawn or not */
		int respawn;
		/* unix domain sockets for parent death detection
		 * [0] parent
		 * [1] child
		 */
		int sock[2];
	} ipv4, ipv6;
	struct sigaction sa;
	int quitsent = 0;
	server_run = 1;
	/* set up the signal handler */
	memset(&sa, 0, sizeof(sa));
	sa.sa_handler = server_quit_handler;
	sa.sa_flags = 0;
	sigfillset(&(sa.sa_mask));
	REGSIG(SIGINT, &sa);
	REGSIG(SIGTERM, &sa);
	REGSIG(SIGQUIT, &sa);
	/* set some initial values for worker state */
	ipv4.pid = ipv4.sock[0] = ipv4.sock[1] = -1;
	ipv6.pid = ipv6.sock[0] = ipv6.sock[1] = -1;
	ipv4.respawn = ipv6.respawn = 1;
	for (;;) {
		int rawstatus, status;
		const char *worker = "UNK";
		pid_t child;
		if (server_run == 1) {
			FORKWORKER("IPv4", cfg->_sock, cfg->_sock6, ipv4, AF_INET);
			FORKWORKER("IPv6", cfg->_sock6, cfg->_sock, ipv6, AF_INET6);
		} else if (quitsent == 0) {
			IPCSEND(ipv4, "quit");
			IPCSEND(ipv6, "quit");
			quitsent = 1;
		}
		errno = 0;
		child = wait(&rawstatus);
		if (child == -1) {
			/* don't print out an error with ECHILD or EINTR */
			if (errno == EINTR) {
				continue;
			}
			if (errno != ECHILD) {
				log_perror(
					&(cfg->_lcfg),
					errno,
					"server: wait"
				);
			}
			break;
		}
		if (WIFEXITED(rawstatus)) {
			status = WEXITSTATUS(rawstatus);
		} else {
			status = -1;
		}
		if (child == ipv4.pid) {
			close(ipv4.sock[0]);
			worker = "IPv4";
		} else if (child == ipv6.pid) {
			close(ipv6.sock[0]);
			worker = "IPv6";
		}
		if (child == ipv4.pid || child == ipv6.pid) {
			if (status == 0) {
				log_ok(
					&(cfg->_lcfg),
					"server: %s worker shut down cleanly",
					worker
				);
			} else if (status > 0) {
				log_err(
					&(cfg->_lcfg),
					"server: %s worker returned %d!",
					worker,
					status
				);
				/* a non-zero exit status means we'll stop
				 * respawning the worker
				 */
				if (child == ipv4.pid) {
					ipv4.respawn = 0;
				} else if (child == ipv6.pid) {
					ipv6.respawn = 0;
				}
			} else if (WIFSIGNALED(rawstatus)) {
				status = WTERMSIG(rawstatus);
				log_err(
					&(cfg->_lcfg),
					"server: %s worker was terminated by "
					"signal %d! (%s)",
					worker,
					status,
					strsignal(status)
				);
			}
			if (child == ipv4.pid) {
				ipv4.pid = -1;
			} else if (child == ipv6.pid) {
				ipv6.pid = -1;
			}
			if (ipv4.pid == -1 && ipv6.pid == -1) {
				log_reg(
					&(cfg->_lcfg),
					"server: All workers finished"
				);
				break;
			}
		} else {
			log_wrn(
				&(cfg->_lcfg),
				"server: An unknown child [%d] died",
				child
			);
		}
	}
}

/* vi: set sts=8 ts=8 sw=8 noexpandtab: */
