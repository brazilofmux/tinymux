/*
 * bsd.c 
 */
/*
 * $Id: bsd.c,v 1.2 1997/04/16 06:00:39 dpassmor Exp $ 
 */
#include "copyright.h"
#include "autoconf.h"

#ifdef VMS
#include "multinet_root:[multinet.include.sys]file.h"
#include "multinet_root:[multinet.include.sys]ioctl.h"
#include "multinet_root:[multinet.include]errno.h"
#else
#include <sys/file.h>
#include <sys/ioctl.h>
#include <sys/wait.h>
#endif
#include <signal.h>

#include "mudconf.h"
#include "config.h"
#include "db.h"
#include "file_c.h"
#include "externs.h"
#include "interface.h"
#include "flags.h"
#include "powers.h"
#include "alloc.h"
#include "command.h"
#include "slave.h"
#include "attrs.h"

#ifdef SOLARIS
extern const int _sys_nsig;

#define NSIG _sys_nsig
#endif

#ifdef CONCENTRATE
extern struct descriptor_data *ccontrol;
extern void FDECL(send_killconcid, (DESC *));
extern long NDECL(make_concid);

#endif

extern void NDECL(dispatch);
extern void NDECL(dump_restart_db);
extern void FDECL(dump_database_internal, (int));
int sock;
int ndescriptors = 0;
int maxd = 0;

DESC *descriptor_list = NULL;
pid_t slave_pid;
int slave_socket = -1;

DESC *FDECL(initializesock, (int, struct sockaddr_in *));
DESC *FDECL(new_connection, (int));
int FDECL(process_output, (DESC *));
int FDECL(process_input, (DESC *));

/*
 * get a result from the slave 
 */
static int get_slave_result()
{
	char *buf;
	char *token;
	char *os;
	char *userid;
	char *host;
	int local_port, remote_port;
	char *p;
	DESC *d;
	unsigned long addr;
	int len;

	buf = alloc_lbuf("slave_buf");

	len = read(slave_socket, buf, LBUF_SIZE - 1);
	if (len < 0) {
		if ((errno == EAGAIN) || (errno == EWOULDBLOCK)) {
			free_lbuf(buf);
			return (-1);
		}
		close(slave_socket);
		slave_socket = -1;
		free_lbuf(buf);
		return (-1);
	} else if (len == 0) {
		free_lbuf(buf);
		return (-1);
	}
	buf[len] = '\0';

	token = alloc_lbuf("slave_token");
	os = alloc_lbuf("slave_os");
	userid = alloc_lbuf("slave_userid");
	host = alloc_lbuf("slave_host");

	if (sscanf(buf, "%s %s",
		   host, token) != 2) {
		free_lbuf(buf);
		free_lbuf(token);
		free_lbuf(os);
		free_lbuf(userid);
		free_lbuf(host);
		return (0);
	}
	p = strchr(buf, '\n');
	*p = '\0';
	for (d = descriptor_list; d; d = d->next) {
		if (strcmp(d->addr, host))
			continue;
		if (mudconf.use_hostname) {
			StringCopyTrunc(d->addr, token, 50);
			d->addr[50] = '\0';
			if (d->player != 0) {
				if (d->username[0])
					atr_add_raw(d->player, A_LASTSITE, tprintf("%s@%s",
						     d->username, d->addr));
				else
					atr_add_raw(d->player, A_LASTSITE, d->addr);
			}
		}
	}

	if (sscanf(p + 1, "%s %d , %d : %s : %s : %s",
		   host,
		   &remote_port, &local_port,
		   token, os, userid) != 6) {
		free_lbuf(buf);
		free_lbuf(token);
		free_lbuf(os);
		free_lbuf(userid);
		free_lbuf(host);
		return (0);
	}
	for (d = descriptor_list; d; d = d->next) {
		if (ntohs((d->address).sin_port) != remote_port)
			continue;
		StringCopyTrunc(d->username, userid, 10);
		d->username[10] = '\0';
		if (d->player != 0) {
			atr_add_raw(d->player, A_LASTSITE, tprintf("%s@%s",
						     d->username, d->addr));
		}
		free_lbuf(buf);
		free_lbuf(token);
		free_lbuf(os);
		free_lbuf(userid);
		free_lbuf(host);
		return (0);
	}
	free_lbuf(buf);
	free_lbuf(token);
	free_lbuf(os);
	free_lbuf(userid);
	free_lbuf(host);
	return (0);
}

void boot_slave()
{
	int sv[2];
	int i;
	int maxfds;

#ifdef HAVE_GETDTABLESIZE
	maxfds = getdtablesize();
#else
	maxfds = sysconf(_SC_OPEN_MAX);
#endif

	if (slave_socket != -1) {
		close(slave_socket);
		slave_socket = -1;
	}
	if (socketpair(AF_UNIX, SOCK_DGRAM, 0, sv) < 0) {
		return;
	}
	/*
	 * set to nonblocking 
	 */
	if (fcntl(sv[0], F_SETFL, FNDELAY) == -1) {
		close(sv[0]);
		close(sv[1]);
		return;
	}
	slave_pid = vfork();
	switch (slave_pid) {
	case -1:
		close(sv[0]);
		close(sv[1]);
		return;

	case 0:		/*
				 * * child  
				 */
		close(sv[0]);
		close(0);
		close(1);
		if (dup2(sv[1], 0) == -1) {
			_exit(1);
		}
		if (dup2(sv[1], 1) == -1) {
			_exit(1);
		}
		for (i = 3; i < maxfds; ++i) {
			close(i);
		}
		execlp("bin/slave", "slave", NULL);
		_exit(1);
	}
	close(sv[1]);

	if (fcntl(sv[0], F_SETFL, FNDELAY) == -1) {
		close(sv[0]);
		return;
	}
	slave_socket = sv[0];
}

int make_socket(port)
int port;
{
	int s, opt;
	struct sockaddr_in server;

	s = socket(AF_INET, SOCK_STREAM, 0);
	if (s < 0) {
		log_perror("NET", "FAIL", NULL, "creating master socket");
		exit(3);
	}
	opt = 1;
	if (setsockopt(s, SOL_SOCKET, SO_REUSEADDR,
		       (char *)&opt, sizeof(opt)) < 0) {
		log_perror("NET", "FAIL", NULL, "setsockopt");
	}
	server.sin_family = AF_INET;
	server.sin_addr.s_addr = INADDR_ANY;
	server.sin_port = htons(port);
	if (!mudstate.restarting)
		if (bind(s, (struct sockaddr *)&server, sizeof(server))) {
			log_perror("NET", "FAIL", NULL, "bind");
			close(s);
			exit(4);
		}
	listen(s, 5);
	return s;
}

#ifndef HAVE_GETTIMEOFDAY
#define get_tod(x)	{ (x)->tv_sec = time(NULL); (x)->tv_usec = 0; }
#else
#define get_tod(x)	gettimeofday(x, (struct timezone *)0)
#endif

void shovechars(port)
int port;
{
	fd_set input_set, output_set;
	struct timeval last_slice, current_time, next_slice, timeout, slice_timeout;
	int found, check;
	DESC *d, *dnext, *newd;
	int avail_descriptors, maxfds;

#define CheckInput(x)	FD_ISSET(x, &input_set)
#define CheckOutput(x)	FD_ISSET(x, &output_set)

	mudstate.debug_cmd = (char *)"< shovechars >";
	if (!mudstate.restarting) {
		sock = make_socket(port);
	}
	if (!mudstate.restarting)
		maxd = sock + 1;

	get_tod(&last_slice);

#ifdef HAVE_GETDTABLESIZE
	maxfds = getdtablesize();
#else
	maxfds = sysconf(_SC_OPEN_MAX);
#endif

	avail_descriptors = maxfds - 7;

	while (mudstate.shutdown_flag == 0) {
		get_tod(&current_time);
		last_slice = update_quotas(last_slice, current_time);

		process_commands();
		if (mudstate.shutdown_flag)
			break;

		/*
		 * test for events 
		 */

		dispatch();

		/*
		 * any queued robot commands waiting? 
		 */

		timeout.tv_sec = que_next();
		timeout.tv_usec = 0;
		next_slice = msec_add(last_slice, mudconf.timeslice);
		slice_timeout = timeval_sub(next_slice, current_time);

		FD_ZERO(&input_set);
		FD_ZERO(&output_set);

		/*
		 * Listen for new connections if there are free descriptors 
		 */

		if (ndescriptors < avail_descriptors) {
			FD_SET(sock, &input_set);
		}
		/*
		 * Listen for replies from the slave socket 
		 */

		if (slave_socket != -1) {
			FD_SET(slave_socket, &input_set);
		}
		/*
		 * Mark sockets that we want to test for change in status 
		 */

		DESC_ITER_ALL(d) {
			if (!d->input_head)
				FD_SET(d->descriptor, &input_set);
			if (d->output_head)
				FD_SET(d->descriptor, &output_set);
		}

		/*
		 * Wait for something to happen 
		 */
		found = select(maxd, &input_set, &output_set, (fd_set *) NULL,
			       &timeout);

		if (found < 0) {
			if (errno != EINTR) {
				log_perror("NET", "FAIL",
					 "checking for activity", "select");
			}
			continue;
		}
		/*
		 * if !found then time for robot commands 
		 */

		if (!found) {
			if (mudconf.queue_chunk)
				do_top(mudconf.queue_chunk);
			continue;
		} else {
			do_top(mudconf.active_q_chunk);
		}

		/*
		 * Get usernames and hostnames 
		 */

		if (slave_socket != -1 &&
		    FD_ISSET(slave_socket, &input_set)) {
			while (get_slave_result() == 0) ;
		}
		/*
		 * Check for new connection requests 
		 */

		if (CheckInput(sock)) {
			newd = new_connection(sock);
			if (!newd) {
				check = (errno && (errno != EINTR) &&
					 (errno != EMFILE) &&
					 (errno != ENFILE));
				if (check) {
					log_perror("NET", "FAIL", NULL,
						   "new_connection");
				}
			} else {
				if (newd->descriptor >= maxd)
					maxd = newd->descriptor + 1;
			}
		}
		/*
		 * Check for activity on user sockets 
		 */

		DESC_SAFEITER_ALL(d, dnext) {

			/*
			 * Process input from sockets with pending input 
			 */

			if (CheckInput(d->descriptor)) {

				/*
				 * Undo autodark 
				 */

				if (d->flags & DS_AUTODARK) {
					d->flags &= ~DS_AUTODARK;
					s_Flags(d->player,
						Flags(d->player) & ~DARK);
				}
				/*
				 * Process received data 
				 */
#ifdef CONCENTRATE
				if (!(d->cstatus & C_REMOTE))
#endif
					if (!process_input(d)) {
						shutdownsock(d, R_SOCKDIED);
						continue;
					}
			}
			/*
			 * Process output for sockets with pending output 
			 */

			if (CheckOutput(d->descriptor)) {
				if (!process_output(d)) {
#ifdef CONCENTRATE
					if (!(d->cstatus & C_CCONTROL))
#endif
						shutdownsock(d, R_SOCKDIED);
				}
			}
		}
	}
}

DESC *new_connection(sock)
int sock;
{
	int newsock;
	char *buff, *buff1, *cmdsave;
	DESC *d;
	struct sockaddr_in addr;
	int addr_len, len;
	char *buf;
	char *chp;


	cmdsave = mudstate.debug_cmd;
	mudstate.debug_cmd = (char *)"< new_connection >";
	addr_len = sizeof(struct sockaddr);

	newsock = accept(sock, (struct sockaddr *)&addr, &addr_len);
	if (newsock < 0)
		return 0;

	if (site_check(addr.sin_addr, mudstate.access_list) == H_FORBIDDEN) {
		STARTLOG(LOG_NET | LOG_SECURITY, "NET", "SITE")
			buff = alloc_mbuf("new_connection.LOG.badsite");
		sprintf(buff, "[%d/%s] Connection refused.  (Remote port %d)",
		   newsock, inet_ntoa(addr.sin_addr), ntohs(addr.sin_port));
		log_text(buff);
		free_mbuf(buff);
		ENDLOG
			fcache_rawdump(newsock, FC_CONN_SITE);
		shutdown(newsock, 2);
		close(newsock);
		errno = 0;
		d = NULL;
	} else {
		buff = alloc_mbuf("new_connection.address");
		buf = alloc_lbuf("new_connection.write");
		StringCopy(buff, inet_ntoa(addr.sin_addr));

		/*
		 * Ask slave process for host and username 
		 */
		if ((slave_socket != -1) && mudconf.use_hostname) {
			sprintf(buf, "%s\n%s,%d,%d\n", inet_ntoa(addr.sin_addr), inet_ntoa(addr.sin_addr), ntohs(addr.sin_port), mudconf.port);
			len = strlen(buf);
			if (WRITE(slave_socket, buf, len) < 0) {
				close(slave_socket);
				slave_socket = -1;
			}
		}
		free_lbuf(buf);
		STARTLOG(LOG_NET, "NET", "CONN")
			buff1 = alloc_mbuf("new_connection.LOG.open");
		sprintf(buff1, "[%d/%s] Connection opened (remote port %d)",
			newsock, buff, ntohs(addr.sin_port));
		log_text(buff1);
		free_mbuf(buff1);
		ENDLOG
			d = initializesock(newsock, &addr);
		mudstate.debug_cmd = cmdsave;
		free_mbuf(buff);
	}
	mudstate.debug_cmd = cmdsave;
	return (d);
}

/*
 * Disconnect reasons that get written to the logfile 
 */

static const char *disc_reasons[] =
{
	"Unspecified",
	"Quit",
	"Inactivity Timeout",
	"Booted",
	"Remote Close or Net Failure",
	"Game Shutdown",
	"Login Retry Limit",
	"Logins Disabled",
	"Logout (Connection Not Dropped)",
	"Too Many Connected Players"
};

/*
 * Disconnect reasons that get fed to A_ADISCONNECT via announce_disconnect 
 */

static const char *disc_messages[] =
{
	"unknown",
	"quit",
	"timeout",
	"boot",
	"netdeath",
	"shutdown",
	"badlogin",
	"nologins",
	"logout"
};

void shutdownsock(d, reason)
DESC *d;
int reason;
{
	char *buff, *buff2;
	time_t now;
	int i, num;
	DESC *dtemp;

	if ((reason == R_LOGOUT) &&
	    (site_check((d->address).sin_addr, mudstate.access_list) == H_FORBIDDEN))
		reason = R_QUIT;

	if (d->flags & DS_CONNECTED) {

		/*
		 * Do the disconnect stuff if we aren't doing a LOGOUT * * *
		 * * * * (which keeps the connection open so the player can *
		 * * connect * * * * to a different character). 
		 */

		if (reason != R_LOGOUT) {
			fcache_dump(d, FC_QUIT);
			STARTLOG(LOG_NET | LOG_LOGIN, "NET", "DISC")
				buff = alloc_mbuf("shutdownsock.LOG.disconn");
			sprintf(buff, "[%d/%s] Logout by ",
				d->descriptor, d->addr);
			log_text(buff);
			log_name(d->player);
			sprintf(buff, " <Reason: %s>",
				disc_reasons[reason]);
			log_text(buff);
			free_mbuf(buff);
			ENDLOG
		} else {
			STARTLOG(LOG_NET | LOG_LOGIN, "NET", "LOGO")
				buff = alloc_mbuf("shutdownsock.LOG.logout");
			sprintf(buff, "[%d/%s] Logout by ",
				d->descriptor, d->addr);
			log_text(buff);
			log_name(d->player);
			sprintf(buff, " <Reason: %s>",
				disc_reasons[reason]);
			log_text(buff);
			free_mbuf(buff);
			ENDLOG
		}

		/*
		 * If requested, write an accounting record of the form: * *
		 * * * * * Plyr# Flags Cmds ConnTime Loc Money [Site]
		 * <DiscRsn>  * *  * Name 
		 */

		STARTLOG(LOG_ACCOUNTING, "DIS", "ACCT")
			now = mudstate.now - d->connected_at;
		buff = alloc_lbuf("shutdownsock.LOG.accnt");
		buff2 = decode_flags(GOD, Flags(d->player),
				     Flags2(d->player), Flags3(d->player));
		sprintf(buff, "%d %s %d %d %d %d [%s] <%s> %s",
			d->player, buff2, d->command_count, now,
			Location(d->player), Pennies(d->player),
			d->addr, disc_reasons[reason],
			Name(d->player));
		log_text(buff);
		free_lbuf(buff);
		free_sbuf(buff2);
		ENDLOG
			announce_disconnect(d->player, d, disc_messages[reason]);
	} else {
		if (reason == R_LOGOUT)
			reason = R_QUIT;
		STARTLOG(LOG_SECURITY | LOG_NET, "NET", "DISC")
			buff = alloc_mbuf("shutdownsock.LOG.neverconn");
		sprintf(buff,
		 "[%d/%s] Connection closed, never connected. <Reason: %s>",
			d->descriptor, d->addr, disc_reasons[reason]);
		log_text(buff);
		free_mbuf(buff);
		ENDLOG
	}
	process_output(d);
	clearstrings(d);
	if (reason == R_LOGOUT) {
		d->flags &= ~DS_CONNECTED;
		d->connected_at = mudstate.now;
		d->retries_left = mudconf.retry_limit;
		d->command_count = 0;
		d->timeout = mudconf.idle_timeout;
		d->player = 0;
		d->doing[0] = '\0';
		d->quota = mudconf.cmd_quota_max;
		d->last_time = 0;
		d->host_info = site_check((d->address).sin_addr,
					  mudstate.access_list) |
			site_check((d->address).sin_addr,
				   mudstate.suspect_list);
		d->input_tot = d->input_size;
		d->output_tot = 0;
		welcome_user(d);
	} else {
#ifdef CONCENTRATE
		if (!(d->cstatus & C_REMOTE)) {
			if (d->cstatus & C_CCONTROL) {
				register struct descriptor_data *k;

				for (k = descriptor_list; k; k = k->next)
					if (k->parent == d)
						shutdownsock(k, R_QUIT);
			}
#endif
			shutdown(d->descriptor, 2);
			close(d->descriptor);
#ifdef CONCENTRATE
		} else {
			register struct descriptor_data *k;

			for (k = descriptor_list; k; k = k->next)
				if (d->parent == k)
					send_killconcid(d);
		}
#endif
		freeqs(d);
		*d->prev = d->next;
		if (d->next)
			d->next->prev = d->prev;

		/*
		 * Is this desc still in interactive mode? 
		 */
		if (d->program_data != NULL) {
			num = 0;
			DESC_ITER_PLAYER(d->player, dtemp) num++;
			
			if (num == 0) {
				for (i = 0; i < MAX_GLOBAL_REGS; i++) {
					free_lbuf(d->program_data->wait_regs[i]);
				}
				free(d->program_data);
			}
		}
			
		free_desc(d);
#ifdef CONCENTRATE
		if (!(d->cstatus & C_REMOTE))
#endif
			ndescriptors--;
	}
}

void make_nonblocking(s)
int s;
{
#ifdef HAVE_LINGER
	struct linger ling;

#endif

#ifdef FNDELAY
	if (fcntl(s, F_SETFL, FNDELAY) == -1) {
		log_perror("NET", "FAIL", "make_nonblocking", "fcntl");
	}
#else
	if (fcntl(s, F_SETFL, O_NDELAY) == -1) {
		log_perror("NET", "FAIL", "make_nonblocking", "fcntl");
	}
#endif
#ifdef HAVE_LINGER
	ling.l_onoff = 0;
	ling.l_linger = 0;
	if (setsockopt(s, SOL_SOCKET, SO_LINGER,
		       (char *)&ling, sizeof(ling)) < 0) {
		log_perror("NET", "FAIL", "linger", "setsockopt");
	}
#endif
}

DESC *initializesock(s, a)
int s;
struct sockaddr_in *a;
{
	DESC *d;
	int i;

	ndescriptors++;
	d = alloc_desc("init_sock");
	d->descriptor = s;
#ifdef CONCENTRATE
	d->concid = make_concid();
	d->cstatus = 0;
	d->parent = 0;
#endif
	d->flags = 0;
	d->connected_at = mudstate.now;
	d->retries_left = mudconf.retry_limit;
	d->command_count = 0;
	d->timeout = mudconf.idle_timeout;
	d->host_info = site_check((*a).sin_addr, mudstate.access_list) |
		site_check((*a).sin_addr, mudstate.suspect_list);
	d->player = 0;		/*
				 * be sure #0 isn't wizard.  Shouldn't be. 
				 */

	d->addr[0] = '\0';
	d->doing[0] = '\0';
	d->username[0] = '\0';
	make_nonblocking(s);
	d->output_prefix = NULL;
	d->output_suffix = NULL;
	d->output_size = 0;
	d->output_tot = 0;
	d->output_lost = 0;
	d->output_head = NULL;
	d->output_tail = NULL;
	d->input_head = NULL;
	d->input_tail = NULL;
	d->input_size = 0;
	d->input_tot = 0;
	d->input_lost = 0;
	d->raw_input = NULL;
	d->raw_input_at = NULL;
	d->quota = mudconf.cmd_quota_max;
	d->program_data = NULL;
	d->last_time = 0;
	d->address = *a;	/*
				 * added 5/3/90 SCG 
				 */
	if (descriptor_list)
		descriptor_list->prev = &d->next;
	d->hashnext = NULL;
	d->next = descriptor_list;
	d->prev = &descriptor_list;
	StringCopyTrunc(d->addr, inet_ntoa(a->sin_addr), 50);
	descriptor_list = d;
	welcome_user(d);
	return d;
}

int process_output(d)
DESC *d;
{
	TBLOCK *tb, *save;
	int cnt;
	char *cmdsave;
	static char buf[LBUF_SIZE];
	int len;

	cmdsave = mudstate.debug_cmd;
	mudstate.debug_cmd = (char *)"< process_output >";

	tb = d->output_head;

#ifdef CONCENTRATE
	if (d->cstatus & C_REMOTE) {
		static char buf[10];
		static char obuf[2048];
		int buflen, k, j;

		sprintf(buf, "%d ", d->concid);
		buflen = strlen(buf);

		bcopy(buf, obuf, buflen);
		j = buflen;

		while (tb != NULL) {
			for (k = 0; k < tb->hdr.nchars; k++) {
				obuf[j++] = tb->hdr.start[k];
				if (tb->hdr.start[k] == '\n') {
					if (d->parent)
						queue_write(d->parent, obuf, j);
					bcopy(buf, obuf, buflen);
					j = buflen;
				}
			}
			d->output_size -= tb->hdr.nchars;
			save = tb;
			tb = tb->hdr.nxt;
			free(save);
			d->output_head = tb;
			if (tb == NULL)
				d->output_tail = NULL;
		}

		if (j > buflen)
			queue_write(d, obuf + buflen, j - buflen);

		return 1;
	} else {
#endif
		while (tb != NULL) {
			while (tb->hdr.nchars > 0) {
				cnt = WRITE(d->descriptor, tb->hdr.start,
					    tb->hdr.nchars);
				if (cnt < 0) {
					mudstate.debug_cmd = cmdsave;
					if (errno == EWOULDBLOCK)
						return 1;
					return 0;
				}
				d->output_size -= cnt;
				tb->hdr.nchars -= cnt;
				tb->hdr.start += cnt;
			}
			save = tb;
			tb = tb->hdr.nxt;
			free(save);
			d->output_head = tb;
			if (tb == NULL)
				d->output_tail = NULL;
		}
#ifdef CONCENTRATE
	}
#endif

	mudstate.debug_cmd = cmdsave;
	return 1;
}

int process_input(d)
DESC *d;
{
	static char buf[LBUF_SIZE];
	int got, in, lost;
	char *p, *pend, *q, *qend;
	char *cmdsave;

	cmdsave = mudstate.debug_cmd;
	mudstate.debug_cmd = (char *)"< process_input >";

	got = in = READ(d->descriptor, buf, sizeof buf);
	if (got <= 0) {
		mudstate.debug_cmd = cmdsave;
		return 0;
	}
	if (!d->raw_input) {
		d->raw_input = (CBLK *) alloc_lbuf("process_input.raw");
		d->raw_input_at = d->raw_input->cmd;
	}
	p = d->raw_input_at;
	pend = d->raw_input->cmd + LBUF_SIZE - sizeof(CBLKHDR) - 1;
	lost = 0;
	for (q = buf, qend = buf + got; q < qend; q++) {
		if (*q == '\n') {
			*p = '\0';
			if (p > d->raw_input->cmd) {
				save_command(d, d->raw_input);
				d->raw_input = (CBLK *) alloc_lbuf("process_input.raw");

				p = d->raw_input_at = d->raw_input->cmd;
				pend = d->raw_input->cmd + LBUF_SIZE -
					sizeof(CBLKHDR) - 1;
			} else {
				in -= 1;	/*
						 * for newline 
						 */
			}
		} else if ((*q == '\b') || (*q == 127)) {
			if (*q == 127)
				queue_string(d, "\b \b");
			else
				queue_string(d, " \b");
			in -= 2;
			if (p > d->raw_input->cmd)
				p--;
			if (p < d->raw_input_at)
				(d->raw_input_at)--;
		} else if (p < pend && isascii(*q) && isprint(*q)) {
			*p++ = *q;
		} else {
			in--;
			if (p >= pend)
				lost++;
		}
	}
	if (p > d->raw_input->cmd) {
		d->raw_input_at = p;
	} else {
		free_lbuf(d->raw_input);
		d->raw_input = NULL;
		d->raw_input_at = NULL;
	}
	d->input_tot += got;
	d->input_size += in;
	d->input_lost += lost;
	mudstate.debug_cmd = cmdsave;
	return 1;
}

void close_sockets(emergency, message)
int emergency;
char *message;
{
	DESC *d, *dnext;

	DESC_SAFEITER_ALL(d, dnext) {
		if (emergency) {

			WRITE(d->descriptor, message, strlen(message));
			if (shutdown(d->descriptor, 2) < 0)
				log_perror("NET", "FAIL", NULL, "shutdown");
			close(d->descriptor);
		} else {
			queue_string(d, message);
			queue_write(d, "\r\n", 2);
			shutdownsock(d, R_GOING_DOWN);
		}
	}
	close(sock);
}

void NDECL(emergency_shutdown)
{
	close_sockets(1, (char *)"Going down - Bye");
}


/*
 * ---------------------------------------------------------------------------
 * * Signal handling routines.
 */

#ifndef SIGCHLD
#define SIGCHLD SIGCLD
#endif

static RETSIGTYPE sighandler();
/* *INDENT-OFF* */

NAMETAB sigactions_nametab[] = {
{(char *)"exit",	3,	0,	SA_EXIT},
{(char *)"default",	1,	0,	SA_DFLT},
{ NULL,			0,	0,	0}};

/* *INDENT-ON* */








void NDECL(set_signals)
{
	signal(SIGALRM, sighandler);
	signal(SIGCHLD, sighandler);
	signal(SIGHUP, sighandler);
	signal(SIGINT, sighandler);
	signal(SIGQUIT, sighandler);
	signal(SIGTERM, sighandler);
	signal(SIGPIPE, SIG_IGN);
	signal(SIGUSR1, sighandler);
	signal(SIGUSR2, sighandler);
	signal(SIGTRAP, sighandler);
#ifdef SIGXCPU
	signal(SIGXCPU, sighandler);
#endif

	signal(SIGILL, sighandler);
#ifdef __linux__
	signal(SIGFPE, SIG_IGN);
#else
	signal(SIGFPE, sighandler);
#endif
	signal(SIGSEGV, sighandler);
	signal(SIGABRT, sighandler);
#ifdef SIGFSZ
	signal(SIGXFSZ, sighandler);
#endif
#ifdef SIGEMT
	signal(SIGEMT, sighandler);
#endif
#ifdef SIGBUS
	signal(SIGBUS, sighandler);
#endif
#ifdef SIGSYS
	signal(SIGSYS, sighandler);
#endif

}

static void unset_signals()
{
	int i;

	for (i = 0; i < NSIG; i++)
		signal(i, SIG_DFL);
	abort();
}

static void check_panicking(sig)
int sig;
{
	int i;

	/*
	 * If we are panicking, turn off signal catching and resignal 
	 */

	if (mudstate.panicking) {
		for (i = 0; i < NSIG; i++)
			signal(i, SIG_DFL);
		kill(getpid(), sig);
	}
	mudstate.panicking = 1;
}

void log_signal(signame)
const char *signame;
{
	STARTLOG(LOG_PROBLEMS, "SIG", "CATCH")
		log_text((char *)"Caught signal ");
	log_text((char *)signame);
	ENDLOG
}

#ifdef HAVE_STRUCT_SIGCONTEXT
static RETSIGTYPE sighandler(sig, code, scp)
int sig, code;
struct sigcontext *scp;

#else
static RETSIGTYPE sighandler(sig, code)
int sig, code;

#endif
{
#ifdef SYS_SIGLIST_DECLARED
#define signames sys_siglist
#else
	static const char *signames[] =
	{
		"SIGZERO", "SIGHUP", "SIGINT", "SIGQUIT",
		"SIGILL", "SIGTRAP", "SIGABRT", "SIGEMT",
		"SIGFPE", "SIGKILL", "SIGBUS", "SIGSEGV",
		"SIGSYS", "SIGPIPE", "SIGALRM", "SIGTERM",
		"SIGURG", "SIGSTOP", "SIGTSTP", "SIGCONT",
		"SIGCHLD", "SIGTTIN", "SIGTTOU", "SIGIO",
		"SIGXCPU", "SIGXFSZ", "SIGVTALRM", "SIGPROF",
		"SIGWINCH", "SIGLOST", "SIGUSR1", "SIGUSR2"};

#endif

	char buff[32];

#ifdef HAVE_UNION_WAIT
	union wait stat;

#else
	int stat;

#endif

	switch (sig) {
	case SIGUSR1:
		do_restart(1,1,0);
		break;
	case SIGALRM:		/*
				 * Timer 
				 */
		mudstate.alarm_triggered = 1;
		break;
	case SIGCHLD:		/*
				 * Change in child status 
				 */
#ifndef SIGNAL_SIGCHLD_BRAINDAMAGE
		signal(SIGCHLD, sighandler);
#endif
#ifdef HAVE_WAIT3
		while (wait3(&stat, WNOHANG, NULL) > 0) ;
#else
		wait(&stat);
#endif
		/* Did the child exit? */
		
		if (WEXITSTATUS(stat) == 8)
			exit(0);
				
		mudstate.dumping = 0;
		break;
	case SIGHUP:		/*
				 * Perform a database dump 
				 */
		log_signal(signames[sig]);
		mudstate.dump_counter = 0;
		break;
	case SIGINT:		/*
				 * Log + ignore 
				 */
		log_signal(signames[sig]);
		break;
	case SIGQUIT:		/*
				 * Normal shutdown 
				 */
	case SIGTERM:
#ifdef SIGXCPU
	case SIGXCPU:
#endif
		check_panicking(sig);
		log_signal(signames[sig]);
		sprintf(buff, "Caught signal %s, exiting.", signames[sig]);
		raw_broadcast(0, buff);
		dump_database_internal(4);
		exit(0);
		break;
	case SIGILL:		/*
				 * Panic save + restart 
				 */
	case SIGFPE:
	case SIGSEGV:
	case SIGTRAP:
#ifdef SIGXFSZ
	case SIGXFSZ:
#endif
#ifdef SIGEMT
	case SIGEMT:
#endif
#ifdef SIGBUS
	case SIGBUS:
#endif
#ifdef SIGSYS
	case SIGSYS:
#endif
		check_panicking(sig);
		log_signal(signames[sig]);
		report();
		if (mudconf.sig_action != SA_EXIT) {
			char outdb[128];
			char indb[128];

			raw_broadcast(0, "Game: Fatal signal %s caught, restarting with previous database.", signames[sig]);
			
			/* Don't sync first. Using older db. */
			
			CLOSE;
			dump_database_internal(1);
			shutdown(slave_socket, 2);
			kill(slave_pid, SIGKILL);

			/*
			 * Try our best to dump a core first 
			 */
			if (!fork()) {
				unset_signals();
				exit(1);
			}
			if (mudconf.compress_db) {
				sprintf(outdb, "%s.Z", mudconf.outdb);
				sprintf(indb, "%s.Z", mudconf.indb);
				rename(outdb, indb);
			} else {
				rename(mudconf.outdb, mudconf.indb);
			}

			alarm(0);
			dump_restart_db();
			execl("bin/netmux", "netmux", mudconf.config_file, NULL);
			break;
		} else {
			unset_signals();
			signal(sig, SIG_DFL);
			exit(1);
		}
	case SIGABRT:		/*
				 * Coredump. 
				 */
		check_panicking(sig);
		log_signal(signames[sig]);
		report();

		unset_signals();
		signal(sig, SIG_DFL);
		exit(1);

	}
	signal(sig, sighandler);
	mudstate.panicking = 0;
#ifdef VMS
	return 1;
#else
	return;
#endif
}
