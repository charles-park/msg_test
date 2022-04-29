//------------------------------------------------------------------------------
//
// 2022.04.14 Argument parser app. (chalres-park)
//
//------------------------------------------------------------------------------
//------------------------------------------------------------------------------
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <limits.h>
#include <netdb.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <linux/fb.h>
#include <linux/sockios.h>
#include <net/if.h>
#include <netinet/in.h>
#include <sys/ioctl.h> 
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>

//------------------------------------------------------------------------------
#include "typedefs.h"
#include "uartlib.h"

//------------------------------------------------------------------------------
static void tolowerstr (char *p)
{
	int i, c = strlen(p);

	for (i = 0; i < c; i++, p++)
		*p = tolower(*p);
}

//------------------------------------------------------------------------------
static void toupperstr (char *p)
{
	int i, c = strlen(p);

	for (i = 0; i < c; i++, p++)
		*p = toupper(*p);
}

//------------------------------------------------------------------------------
static void print_usage(const char *prog)
{
	printf("Usage: %s [-Dsbd]\n", prog);
	puts("  -D --device        device name. (default /dev/ttyUSB0).\n"
		 "  -b --baudrate      serial baudrate.(default 115200bps n81)\n"
		 "  -s --server        server mode enable (default client mode).\n"
	     "  -d --delay         data r/w delay (default = 1 sec)\n"
	);
	exit(1);
}

//------------------------------------------------------------------------------
static char 	*OPT_DEVICE_NAME = "/dev/ttyUSB0";
static bool		OPT_SERVER_MODE = false;;
static int 		OPT_BAUDRATE = 115200, OPT_DELAY = 1;

//------------------------------------------------------------------------------
static void parse_opts (int argc, char *argv[])
{
	while (1) {
		static const struct option lopts[] = {
			{ "device_name",	1, 0, 'D' },
			{ "server_mode",	0, 0, 's' },
			{ "baudrate",		1, 0, 'b' },
			{ "delay",			1, 0, 'd' },
			{ NULL, 0, 0, 0 },
		};
		int c;

		c = getopt_long(argc, argv, "D:sb:d:", lopts, NULL);

		if (c == -1)
			break;

		switch (c) {
		case 'D':
			tolowerstr (optarg);
			OPT_DEVICE_NAME = optarg;
			break;
		case 's':
			OPT_SERVER_MODE = true;
			break;
		case 'b':
			OPT_BAUDRATE = atoi(optarg);
			break;
		case 'd':
			OPT_DELAY = atoi(optarg);
			break;
		default:
			print_usage(argv[0]);
			break;
		}
	}
}

//------------------------------------------------------------------------------
#pragma pack(1)
typedef struct msg__t {
	char	cmd;
	char	msg[20];
	char	data[20];
}	msg_t;

#pragma pack(1)
typedef struct protocol__t {
	char	header;
	msg_t	*pmsg;
	char	tail;
}	protocol_t;


//------------------------------------------------------------------------------
int chk_func(ptc_var_t *var)
{
	if(var->buf[(var->p_sp + var->size -1) % var->size] != '#')	return 0;
	if(var->buf[(var->p_sp               ) % var->size] != '@')	return 0;

	return 1;
}

//------------------------------------------------------------------------------
int cat_func (ptc_var_t *var)
{
	char *p = (char *)var->arg;
	int i;

	memset (p, 0, sizeof(msg_t));

	/* receive data save */
	for (i = 0; i < sizeof(msg_t); i++)
		p[i] = var->buf[(var->p_sp + 1 + i) % var->size];

	return 1;
}

//------------------------------------------------------------------------------
void send_ack (ptc_grp_t *ptc_grp, msg_t *m)
{
	protocol_t ack;
	int i;
	char *p = (char *)&ack;

	ack.header 	= '@';	ack.tail   	= '#';
	ack.pmsg	= m;

	for (i = 0; i < sizeof(protocol_t); i++)
		queue_put (&ptc_grp->dq, p + i);
}

//------------------------------------------------------------------------------
int send_boot_cmd(ptc_grp_t *ptc_grp)
{
	int w_cnt = 0;

	while (true) {
		// send boot msg
		if ((w_cnt % 5) == 0) {
			msg_t s_msg;
			s_msg.cmd = 'b';
			info ("send boot cmd = %c\n", s_msg.cmd);
			send_ack (ptc_grp, &s_msg);
			w_cnt = 0;
		}

		if (ptc_grp->p[0].var.pass) {
			msg_t *r_msg = (msg_t *)ptc_grp->p[0].var.arg;
			ptc_grp->p[0].var.pass = false;
			ptc_grp->p[0].var.open = true;

			if (r_msg->cmd == 'b') {
				info ("ack msg %s %s\n", r_msg->data, r_msg->msg);
				return 1;
			}
		}
		sleep(1);	w_cnt++;
	}
	return 0;
}

//------------------------------------------------------------------------------
int wait_boot_cmd(ptc_grp_t *ptc_grp)
{
	int w_cnt = 0;
	while (true) {
		if (ptc_grp->p[0].var.pass) {
			msg_t *r_msg = (msg_t *)ptc_grp->p[0].var.arg;
			msg_t s_msg;
			ptc_grp->p[0].var.pass = false;
			ptc_grp->p[0].var.open = true;

			if (r_msg->cmd == 'b') {
				info ("boot complete. i'm ready...\n");
				s_msg.cmd = r_msg->cmd;
				sprintf(s_msg.msg,"%s", "ready");
				sprintf(s_msg.data, "wait %d", w_cnt);
				send_ack (ptc_grp, &s_msg);
				return 1;
			}
		}
		sleep(1);	w_cnt++;
		// if (timeout) return 0;
	}
	return 0;
}

//------------------------------------------------------------------------------
int main(int argc, char **argv)
{
	int fd, speed, wait = 0, p_cnt;
	ptc_grp_t *ptc_grp;
	msg_t r_msg;
	char ascii = 'A';

    parse_opts(argc, argv);

	if ((fd = uart_init (OPT_DEVICE_NAME, B115200)) < 0)
		return 0;

	if (!(ptc_grp = ptc_grp_init (fd, 1)))
		return 0;

	if (!ptc_func_init (ptc_grp, 0, 10, chk_func, cat_func, &r_msg))
		goto out;

	if (OPT_SERVER_MODE){
		info ("Server mode enable!!\n");
		if (!wait_boot_cmd(ptc_grp))	goto out;
	}
	else {
		info ("Client mode enable!!\n");
		if (!send_boot_cmd(ptc_grp))	goto out;
	}

	while (true) {
		for (p_cnt = 0; p_cnt < ptc_grp->pcnt; p_cnt++) {
			if (OPT_SERVER_MODE) {
				// send boot msg
				if ((wait % 3) == 0) {
					msg_t s_msg;
					s_msg.cmd = ascii;
					send_ack (ptc_grp, &s_msg);
					info ("send cmd = %c\n", s_msg.cmd);
				}
				if (ptc_grp->p[0].var.pass) {
					msg_t *r_msg = (msg_t *)ptc_grp->p[0].var.arg;
					ptc_grp->p[0].var.pass = false;
					ptc_grp->p[0].var.open = true;

					if (r_msg->cmd == ascii) {
						info ("ack msg %s %s\n", r_msg->data, r_msg->msg);
						ascii = (ascii + 1) < 'Z' ?	(ascii +1) : 'A';
					}
				}
			} else {
				if (ptc_grp->p[p_cnt].var.pass) {
					msg_t s_msg;
					time_t t;
					time(&t);
					ptc_grp->p[0].var.pass = false;
					ptc_grp->p[0].var.open = true;
					info ("Pass cmd = %c, protocol no %d\n", r_msg.cmd, p_cnt);
					s_msg.cmd  = r_msg.cmd;
					sprintf (s_msg.data, "received p = %d", p_cnt);
					sprintf (s_msg.msg, "%s", ctime(&t));
					send_ack(ptc_grp, &s_msg);
				}
			}
		}
		sleep(1);
		info ("wait %d\n", wait++);
		fflush(stdout);
	}
out:
	uart_close(fd);
	ptc_grp_close(ptc_grp);
	return 0;
}

//------------------------------------------------------------------------------
//------------------------------------------------------------------------------
