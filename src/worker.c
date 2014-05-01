#include "worker.h"
#include "log.h"
#include "net.h"
#include "request.h"
#include <unistd.h>
#include <stdlib.h>
#include <poll.h>
#include <string.h>
#include <errno.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <time.h>

/* maximum number of request forks per worker */
#define MAX_REQ_CHILDREN 8

void worker_loop(const struct log_cfg *lcfg, int ipcsock, int af, int sockfd) {
	/* only touch forks_avail from this worker, not its child */
	int pollret = -1, forks_avail = MAX_REQ_CHILDREN, ret = EXIT_SUCCESS;
	const char *worker_name = (af == AF_INET) ? "ipv4" : "ipv6";
	struct pollfd pfd[2];
	/* how long the worker took to send the request down the chain */
	struct timespec tp_b, tp_e;
	double dt;
	union {
		struct sockaddr_in addr4;
		struct sockaddr_in6 addr6;
	} a;
	if (af != AF_INET && af != AF_INET6) {
		log_err(
			lcfg,
			"%s: Unsupported address family!",
			worker_name
		);
		return;
	}
	errno = 0;
	/* the socket to listen to incoming connections */
	pfd[0].fd = sockfd;
	pfd[0].events = POLLIN;
	/* ipc */
	pfd[1].fd = ipcsock;
	pfd[1].events = POLLIN;
	log_ok(
		lcfg,
		"%s worker ready, PID %d",
		(af == AF_INET) ? "IPv4" : "IPv6",
		(int) getpid()
	);
	for (;;) {
		int sockpass;
		pid_t child = -1;
		pfd[0].revents = 0;
		pfd[1].revents = 0;
		errno = 0;
		while (
			(pollret = poll(pfd, 2, 250)) == 0 ||
			forks_avail == 0
		) {
			/* clean up children; block if forks_avail == 0
			 * no need to wait if forks_avail == MAX_REQ_CHILDREN
			 */
			while (
				forks_avail < MAX_REQ_CHILDREN &&
				waitpid(
					-1,
					NULL,
					(forks_avail == 0) ? 0 : WNOHANG
				) > 0
			) {
				forks_avail += 1;
			}
			errno = 0;
		}
		/* handle polling error */
		if (pollret == -1) {
			log_perror(
				lcfg,
				errno,
				"%s: poll",
				worker_name
			);
			ret = EXIT_FAILURE;
			break;
		}
		/* read control messages, 4 bytes each */
		if (pfd[1].revents != 0) {
			char ipcbuf[4];
			int ipcret = -1, pollerr = 0, die = 0;
			size_t ipcoff = 0;
			for (
				errno = 0;
				poll(&(pfd[1]), 1, 0) > 0;
				pollerr = errno
			) {
				die = 0;
				errno = 0;
				ipcret = read(
					pfd[1].fd,
					&(ipcbuf[ipcoff]),
					sizeof(ipcbuf) - ipcoff
				);
				/* parent is dead */
				if (ipcret <= 0) {
					log_perror(
						lcfg,
						errno,
						"%s: read",
						worker_name
					);
					break;
				}
				ipcoff += ipcret;
				if (ipcoff < sizeof(ipcbuf)) {
					continue;
				}
#define MSGCHK(a, b) \
	(memcmp(a, b, sizeof(a)) == 0)
				if (MSGCHK(ipcbuf, "quit")) {
					log_reg(
						lcfg,
						"%s: %s",
						worker_name,
						"Calling it quits..."
					);
					die = 1;
					ret = EXIT_SUCCESS;
					break;
				}
			}
			if (die == 1) {
				break;
			}
			/* log polling error */
			log_perror(
				lcfg,
				pollerr,
				"%s: poll",
				worker_name
			);
			/* break out if the parent is dead */
			if (ipcret <= 0) {
				log_err(
					lcfg,
					"%s: %s",
					worker_name,
					"Parent killed!"
				);
				close(pfd[1].fd);
				ret = EXIT_FAILURE;
				break;
			}
		}
		/* no events */
		if (pfd[0].revents == 0) {
			continue;
		}
		forks_avail -= 1;
		clock_gettime(CLOCK_MONOTONIC, &tp_b);
		errno = 0;
		sockpass = net_accept(
			lcfg,
			sockfd,
			af,
			(af == AF_INET) ?
				(struct sockaddr *) &(a.addr4) :
				(struct sockaddr *) &(a.addr6)
		);
		if (sockpass == -1) {
			int die;
			switch (errno) {
			case EAGAIN:
			case EPROTO:
			case ENOPROTOOPT:
			case EHOSTDOWN:
			case ENONET:
			case EHOSTUNREACH:
			case EOPNOTSUPP:
			case ENETUNREACH:
				die = 0;
				break;
			default:
				die = 1;
			}
			if (die) {
				ret = EXIT_FAILURE;
				break;
			}
			continue;
		}
		/* wait for a fork slot */
		errno = 0;
		child = fork();
		if (child == 0) {
			int childret;
			/* Calculate wait time */
			clock_gettime(CLOCK_MONOTONIC, &tp_e);
			dt = (double) (
				(double) tp_e.tv_sec - (double) tp_b.tv_sec +
				(
					(double) tp_e.tv_nsec -
					(double) tp_b.tv_nsec
				) / (double) 1000000000.0
			);
			/* We won't be needing this anymore, close it
			 * so we won't run out of file descriptors
			 */
			close(sockfd);
			/* Handle the request */
			childret = request_process(
				lcfg,
				sockpass,
				dt,
				(af == AF_INET) ?
					(struct sockaddr *) &(a.addr4) :
					(struct sockaddr *) &(a.addr6)
			);
			/* Exit with child's status */
			exit(childret);
		} else if (child == -1) {
			log_perror(
				lcfg,
				errno,
				"%s: fork",
				worker_name
			);
		}
		/* Close the worker-side part of the socket */
		close(sockpass);
		errno = 0;
	}
	log_reg(
		lcfg,
		"%s: %s",
		worker_name,
		"Waiting for children to terminate"
	);
	for (;wait(NULL) > 0;);
	log_reg(
		lcfg,
		"%s: %s",
		worker_name,
		"Done"
	);
	exit(ret);
}

/* vi: set sts=8 ts=8 sw=8 noexpandtab: */
