/*
 * This slave does iptoname conversions, and identquery lookups.
 * 
 * The philosophy is to keep this program as simple/small as possible.
 * It does normal fork()s, so the smaller it is, the faster it goes.
 * 
 * $Id: slave.c,v 1.2 1997/04/16 06:01:51 dpassmor Exp $
 */

#include "autoconf.h"

#include <netdb.h>
#include <netinet/in.h>
#include <sys/wait.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <signal.h>
#include "slave.h"
#include <arpa/inet.h>

pid_t parent_pid;

#define MAX_STRING 8000
char *arg_for_errors;

char *format_inet_addr(dest, addr)
char *dest;
long addr;
{
	int temp;

	sprintf(dest, "%ld.%ld.%ld.%ld",
		(addr & 0xFF000000) >> 24,
		(addr & 0x00FF0000) >> 16,
		(addr & 0x0000FF00) >> 8,
		(addr & 0x000000FF));
	return (dest + strlen(dest));
}

/*
 * copy a string, returning pointer to the null terminator of dest 
 */
char *stpcpy(dest, src)
char *dest;
const char *src;
{
	while ((*dest = *src)) {
		++dest;
		++src;
	}
	return (dest);
}

void child_timeout_signal()
{
	exit(1);
}

int query(ip, orig_arg)
char *ip;
char *orig_arg;
{
	char *comma;
	char *port_pair;
	struct hostent *hp;
	struct sockaddr_in sin;
	int s;
	FILE *f;
	char result[MAX_STRING];
	char buf[MAX_STRING];
	char buf2[MAX_STRING];
	char buf3[MAX_STRING];
	char arg[MAX_STRING];
	size_t len;
	char *p;
	unsigned long addr;


	addr = inet_addr(ip);
	if (addr == -1) {
		return (-1);
	}
	hp = gethostbyaddr((char *)&addr, sizeof(addr), AF_INET);
	if (hp && !isdigit(*(hp->h_name))) {
		p = stpcpy(buf, ip);
		*p++ = ' ';
		p = stpcpy(p, hp->h_name);
		*p++ = '\n';
		*p++ = '\0';
	} else {
		sprintf(buf, "%s %s\n", ip, ip);
	}
	arg_for_errors = orig_arg;
	strcpy(arg, orig_arg);
	comma = (char *)strrchr(arg, ',');
	if (comma == NULL) {
		return (-1);
	}
	*comma = 0;
	port_pair = (char *)strrchr(arg, ',');
	if (port_pair == NULL) {
		return (-1);
	}
	*port_pair++ = 0;
	*comma = ',';

	hp = gethostbyname(arg);
	if (hp == NULL) {
		static struct hostent def;
		static struct in_addr defaddr;
		static char *alist[1];
		static char namebuf[128];

		defaddr.s_addr = inet_addr(arg);
		if (defaddr.s_addr == -1) {
			return (-1);
		}
		strcpy(namebuf, arg);
		def.h_name = namebuf;
		def.h_addr_list = alist;
		def.h_addr = (char *)&defaddr;
		def.h_length = sizeof(struct in_addr);

		def.h_addrtype = AF_INET;
		def.h_aliases = 0;
		hp = &def;
	}
	sin.sin_family = hp->h_addrtype;
	bcopy(hp->h_addr, (char *)&sin.sin_addr, hp->h_length);
	sin.sin_port = htons(113);	/*
					 * ident port 
					 */
	s = socket(hp->h_addrtype, SOCK_STREAM, 0);
	if (s < 0) {
		return (-1);
	}
	if (connect(s, (struct sockaddr *)&sin, sizeof(sin)) < 0) {
		if (errno != ECONNREFUSED
		    && errno != ETIMEDOUT
		    && errno != ENETUNREACH
		    && errno != EHOSTUNREACH) {
			close(s);
			return (-1);
		}
		buf2[0] = '\0';
	} else {
		len = strlen(port_pair);
		if (write(s, port_pair, len) != len) {
			close(s);
			return (-1);
		}
		if (write(s, "\r\n", 2) != 2) {
			close(s);
			return (-1);
		}
		f = fdopen(s, "r");
		{
			int c;

			p = result;
			while ((c = fgetc(f)) != EOF) {
				if (c == '\n')
					break;
				if (isprint(c)) {
					*p++ = c;
					if (p - result == MAX_STRING - 1)
						break;
				}
			}
			*p = '\0';
		}
		(void)fclose(f);
		p = (char *)format_inet_addr(buf2, ntohl(sin.sin_addr.s_addr));
		*p++ = ' ';
		p = stpcpy(p, result);
		*p++ = '\n';
		*p++ = '\0';
	}
	sprintf(buf3, "%s%s", buf, buf2);
	write(1, buf3, strlen(buf3));
	return (0);
}

void child_signal()
{
	/*
	 * collect any children 
	 */

#ifdef NEXT
	while (wait3(NULL, WNOHANG, NULL) > 0);
#else
	while (waitpid(0, NULL, WNOHANG) > 0) ;
#endif
	signal(SIGCHLD, child_signal);
}

void alarm_signal()
{
	struct itimerval itime;
	struct timeval interval;

	if (getppid() != parent_pid) {
		exit(1);
	}
	signal(SIGALRM, alarm_signal);
	interval.tv_sec = 120;	/*
				 * 2 minutes 
				 */
	interval.tv_usec = 0;
	itime.it_interval = interval;
	itime.it_value = interval;
	setitimer(ITIMER_REAL, &itime, 0);
}


void main(argc, argv)
int argc;
char **argv;
{
	char arg[MAX_STRING + 1];
	char *p;
	int len;

	parent_pid = getppid();
	if (parent_pid == 1) {
		exit(1);
	}
	alarm_signal();
	signal(SIGCHLD, child_signal);
	signal(SIGPIPE, SIG_DFL);

	for (;;) {
		len = read(0, arg, MAX_STRING);
		if (len == 0)
			break;
		if (len < 0) {
			if (errno == EINTR) {
				errno = 0;
				continue;
			}
			break;
		}
		arg[len] = '\0';
		p = strchr(arg, '\n');
		if (p)
			*p = '\0';
		switch (fork()) {
		case -1:
			exit(1);

		case 0:	/*
				 * child 
				 */
			{
				/*
				 * we don't want to try this for more than 5
				 * * * * * minutes 
				 */
				struct itimerval itime;
				struct timeval interval;

				interval.tv_sec = 300;	/*
							 * 5 minutes 
							 */
				interval.tv_usec = 0;
				itime.it_interval = interval;
				itime.it_value = interval;
				signal(SIGALRM, child_timeout_signal);
				setitimer(ITIMER_REAL, &itime, 0);
			}
			exit(query(arg, p + 1) != 0);

		}
		/*
		 * collect any children 
		 */
#ifdef NEXT
		while (wait3(NULL, WNOHANG, NULL) > 0) ;
#else
	 	while (waitpid(0, NULL, WNOHANG) > 0) ;
#endif
	}
	exit(0);
}
