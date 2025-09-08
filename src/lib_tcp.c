/*
 * tcp_lib.c - routines for managing TCP connections.
 *
 * Positive port/program numbers are RPC ports, negative ones are TCP ports.
 *
 * Copyright (c) 1994-1996 Larry McVoy.
 */
#define _LIB /* bench.h needs this */
#include "bench.h"
#include <sys/ioctl.h>
#include <fcntl.h>
#include <linux/vm_sockets.h>

/*
 * Get a TCP socket, bind it, figure out the port,
 * and advertise the port as program "prog".
 *
 * XXX - it would be nice if you could advertise ascii strings.
 */
int tcp_server(int vsock_cid, char *addr, int backlog, int prog, int rdwr)
{
	int sock;
	struct sockaddr_in s_ip;
	struct sockaddr_vm s_vm;

#ifdef LIBTCP_VERBOSE
	fprintf(stderr, "tcp_server(%s, %u, %u, %u)\n", backlog, addr, prog, rdwr);
#endif
	if (vsock_cid)
	{
		if ((sock = socket(AF_VSOCK, SOCK_STREAM, 0)) < 0)
		{
			perror("socket");
			exit(1);
		}
	}
	else if ((sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0)
	{
		perror("socket");
		exit(1);
	}
	sock_optimize(sock, rdwr);
	bzero((void *)&s_ip, sizeof(s_ip));
	bzero((void *)&s_vm, sizeof(s_vm));

	if (inet_aton(addr, &s_ip.sin_addr) < 0)
	{
		fprintf(stderr, "inet_aton cannot parse %s", addr);
		exit(1);
	}

	s_ip.sin_family = AF_INET;
	s_vm.svm_family = AF_VSOCK;

	if (prog < 0)
	{
		s_ip.sin_port = htons(-prog);
		s_vm.svm_port = htons(-prog);
	}

	if (vsock_cid)
	{
		// Read /dev/vsock to get the CID of the host

		int vsock_fd = open("/dev/vsock", O_RDONLY);
		unsigned int cid = 0;

		if (ioctl(vsock_fd, IOCTL_VM_SOCKETS_GET_LOCAL_CID, &cid) < 0)
		{
			perror("ioctl(IOCTL_VM_SOCKETS_GET_LOCAL_CID) failed");
			return -1;
		}

		printf("cid = %u\n", cid);

		s_vm.svm_cid = cid;
		if (bind(sock, (struct sockaddr *)&s_vm, sizeof(s_vm)) < 0)
		{
			printf("value, vsock_cid = %d\n", vsock_cid);
			printf("svm cid = %d\n", s_vm.svm_cid);
			perror("bind 111 ");
			exit(2);
		}
	}
	else if (bind(sock, (struct sockaddr *)&s_ip, sizeof(s_ip)) < 0)
	{
		perror("bind 222 ");
		exit(2);
	}
	if (listen(sock, backlog) < 0)
	{
		perror("listen");
		exit(4);
	}
	if (prog > 0)
	{
#ifdef LIBTCP_VERBOSE
		fprintf(stderr, "Server port %d\n", sockport(sock));
#endif
		(void)pmap_unset((u_long)prog, (u_long)1);
		if (!pmap_set((u_long)prog, (u_long)1, (u_long)IPPROTO_TCP,
					  (unsigned short)sockport(sock)))
		{
			perror("pmap_set");
			exit(5);
		}
	}
	return (sock);
}

/*
 * Unadvertise the socket
 */
int tcp_done(int prog)
{
	if (prog > 0)
	{
		pmap_unset((u_long)prog, (u_long)1);
	}
	return (0);
}

/*
 * Accept a connection and return it
 */
int tcp_accept(int sock, int rdwr)
{
	struct sockaddr s;
	int newsock, namelen;

	namelen = sizeof(s);
	bzero((void *)&s, namelen);

retry:
	if ((newsock = accept(sock, (struct sockaddr *)&s, &namelen)) < 0)
	{
		if (errno == EINTR)
			goto retry;
		perror("accept");
		exit(6);
	}
#ifdef LIBTCP_VERBOSE
	fprintf(stderr, "Server newsock port %d\n", sockport(newsock));
#endif
	sock_optimize(newsock, rdwr);
	return (newsock);
}

/*
 * Connect to the TCP socket advertised as "prog" on "host" and
 * return the connected socket.
 *
 * Hacked Thu Oct 27 1994 to cache pmap_getport calls.  This saves
 * about 4000 usecs in loopback lat_connect calls.  I suppose we
 * should time gethostbyname() & pmap_getprot(), huh?
 */
int tcp_connect(unsigned int vsock_cid, char *host, int prog, int rdwr)
{
	static struct hostent *h;
	static struct sockaddr_in s_ip;
	static struct sockaddr_vm s_vm;
	static u_short save_port;
	static u_long save_prog;
	static char *save_host;
	int sock;
	static int tries = 0;

	if (vsock_cid)
	{
		if ((sock = socket(AF_VSOCK, SOCK_STREAM, 0)) < 0)
		{
			perror("socket");
			exit(1);
		}
	}
	else if ((sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0)
	{
		perror("socket");
		exit(1);
	}
	if (rdwr & SOCKOPT_PID)
	{
		static unsigned short port;
		struct sockaddr_in sin;
		struct sockaddr_vm svm;

		if (!port)
		{
			port = (unsigned short)(getpid() << 4);
			if (port < 1024)
			{
				port += 1024;
			}
		}

		if (vsock_cid)
		{
			do
			{
				port++;
				bzero((void *)&sin, sizeof(sin));
				svm.svm_cid = vsock_cid;
				svm.svm_family = AF_VSOCK;
				svm.svm_port = htons(port);
			} while (bind(sock, (struct sockaddr *)&svm, sizeof(svm)) == -1);
		}
		else
		{
			do
			{
				port++;
				bzero((void *)&sin, sizeof(sin));
				sin.sin_family = AF_INET;
				sin.sin_port = htons(port);
			} while (bind(sock, (struct sockaddr *)&sin, sizeof(sin)) == -1);
		}
	}
#ifdef LIBTCP_VERBOSE
	else
	{
		struct sockaddr_in sin;

		bzero((void *)&sin, sizeof(sin));
		sin.sin_family = AF_INET;
		if (bind(sock, (struct sockaddr *)&sin, sizeof(sin)) < 0)
		{
			perror("bind");
			exit(2);
		}
	}
	fprintf(stderr, "Client port %d\n", sockport(sock));
#endif
	sock_optimize(sock, rdwr);
	if (!h || host != save_host || prog != save_prog)
	{
		save_host = host; /* XXX - counting on them not
						   * changing it - benchmark only.
						   */
		save_prog = prog;
		if (!(h = gethostbyname(host)))
		{
			perror(host);
			exit(2);
		}
		bzero((void *)&s_ip, sizeof(s_ip));
		bzero((void *)&s_vm, sizeof(s_vm));
		s_ip.sin_family = AF_INET;
		s_vm.svm_family = AF_VSOCK;
		s_vm.svm_cid = vsock_cid;
		bcopy((void *)h->h_addr, (void *)&s_ip.sin_addr, h->h_length);
		if (prog > 0)
		{
			save_port = pmap_getport(&s_ip, prog,
									 (u_long)1, IPPROTO_TCP);
			if (!save_port)
			{
				perror("lib TCP: No port found");
				exit(3);
			}
#ifdef LIBTCP_VERBOSE
			fprintf(stderr, "Server port %d\n", save_port);
#endif
			s_ip.sin_port = htons(save_port);
			s_vm.svm_port = htons(save_port);
		}
		else
		{
			s_ip.sin_port = htons(-prog);
			s_vm.svm_port = htons(-prog);
		}
	}
	if (vsock_cid)
	{
		if (connect(sock, (struct sockaddr *)&s_vm, sizeof(s_vm)) < 0)
		{
			if (errno == ECONNRESET || errno == ECONNREFUSED || errno == EAGAIN)
			{
				close(sock);
				if (++tries > 10)
					return (-1);
				return (tcp_connect(vsock_cid, host, prog, rdwr));
			}
			printf("value, vsock_cid = %d\n", vsock_cid);
			printf("port = %d\n", s_vm.svm_port);
			printf("cid = %d\n", s_vm.svm_cid);
			printf("family = %d\n", s_vm.svm_family);
			perror("connect vsock");
			exit(4);
		}
	}
	else if (connect(sock, (struct sockaddr *)&s_ip, sizeof(s_ip)) < 0)
	{
		if (errno == ECONNRESET || errno == ECONNREFUSED || errno == EAGAIN)
		{
			close(sock);
			if (++tries > 10)
				return (-1);
			return (tcp_connect(vsock_cid, host, prog, rdwr));
		}
		perror("connect ip");
		exit(4);
	}
	tries = 0;
	return (sock);
}

void sock_optimize(int sock, int flags)
{
	if (flags & SOCKOPT_READ)
	{
		int sockbuf = SOCKBUF;

		while (setsockopt(sock, SOL_SOCKET, SO_RCVBUF, &sockbuf,
						  sizeof(int)))
		{
			sockbuf >>= 1;
		}
#ifdef LIBTCP_VERBOSE
		fprintf(stderr, "sockopt %d: RCV: %dK\n", sock, sockbuf >> 10);
#endif
	}
	if (flags & SOCKOPT_WRITE)
	{
		int sockbuf = SOCKBUF;

		while (setsockopt(sock, SOL_SOCKET, SO_SNDBUF, &sockbuf,
						  sizeof(int)))
		{
			sockbuf >>= 1;
		}
#ifdef LIBTCP_VERBOSE
		fprintf(stderr, "sockopt %d: SND: %dK\n", sock, sockbuf >> 10);
#endif
	}
	if (flags & SOCKOPT_REUSE)
	{
		int val = 1;
		if (setsockopt(sock, SOL_SOCKET,
					   SO_REUSEADDR, &val, sizeof(val)) == -1)
		{
			perror("SO_REUSEADDR");
		}
	}
}

int sockport(int s)
{
	int namelen;
	struct sockaddr_in sin;

	namelen = sizeof(sin);
	if (getsockname(s, (struct sockaddr *)&sin, &namelen) < 0)
	{
		perror("getsockname");
		return (-1);
	}
	return ((int)ntohs(sin.sin_port));
}
