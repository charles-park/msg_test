//------------------------------------------------------------------------------
// C library headers
//------------------------------------------------------------------------------
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

//------------------------------------------------------------------------------
// Linux headers
//------------------------------------------------------------------------------
#include "uartlib.h"

//------------------------------------------------------------------------------
bool        queue_put       (queue_t *q, __u8 *d);
bool        queue_get       (queue_t *q, __u8 *d);
void        *rx_thread_func (void *arg);
void        *tx_thread_func (void *arg);
void        ptc_set_status  (ptc_grp_t *ptc_grp, __u8 ptc_num, bool status);
void        ptc_q           (ptc_grp_t *ptc_grp, __u8 ptc_num, __u8 idata);
void        ptc_event       (ptc_grp_t *ptc_grp, __u8 idata);
__u8        ptc_func_init   (ptc_grp_t *ptc_grp, __u8 ptc_num, __u8 ptc_size, 
        int (*chk_func)(ptc_var_t *var), int (*cat_func)(ptc_var_t *var), void *arg);
ptc_grp_t   *ptc_grp_init   (int fd, __u8 ptc_count);
void        ptc_grp_close   (ptc_grp_t *ptc_grp);
int         uart_init       (const char *dev_name, speed_t baud);
void        uart_close      (int fd);

//------------------------------------------------------------------------------
//------------------------------------------------------------------------------
bool queue_put (queue_t *q, __u8 *d)
{
    q->buf[q->ep++] = *d;
    if (q->ep >= q->size)   q->ep = 0;
    // queue overflow
    if (q->ep == q->sp) {
        q->sp++;
        return false;
    }
    return  true;
}

//------------------------------------------------------------------------------
bool queue_get (queue_t *q, __u8 *d)
{
    if (q->ep != q->sp) {
        *d = q->buf[q->sp++];
        if (q->sp >= q->size)   q->sp = 0;
        return  true;
    }
    // queue empty
    return false;
}

//------------------------------------------------------------------------------
void *rx_thread_func (void *arg)
{
    __u8 d;
    ptc_grp_t *ptc_grp = (ptc_grp_t *)arg;

    while(true) {
        if (read (ptc_grp->fd, &d, 1))      ptc_event (ptc_grp, d);
        usleep(50);
    }
}

//------------------------------------------------------------------------------
void *tx_thread_func (void *arg)
{
    __u8 d;
    ptc_grp_t *ptc_grp = (ptc_grp_t *)arg;

    while(true) {
        if (queue_get(&ptc_grp->dq, &d))   write (ptc_grp->fd, &d, 1);
        usleep(50);
    }
}

//------------------------------------------------------------------------------
//   Protocol Open & Close Function
//------------------------------------------------------------------------------
void ptc_set_status (ptc_grp_t *ptc_grp, __u8 ptc_num, bool status)
{
    ptc_grp->p[ptc_num].var.open = status;
}

//------------------------------------------------------------------------------
// data save to protocol q
//------------------------------------------------------------------------------
void ptc_q (ptc_grp_t *ptc_grp, __u8 ptc_num, __u8 idata)
{
	ptc_grp->p[ptc_num].var.p_ep %= ptc_grp->p[ptc_num].var.size;
	if (ptc_grp->p[ptc_num].var.p_ep == ptc_grp->p[ptc_num].var.p_sp)
	{
		ptc_grp->p[ptc_num].var.p_sp++;
		ptc_grp->p[ptc_num].var.p_sp %= ptc_grp->p[ptc_num].var.size;
	}
	ptc_grp->p[ptc_num].var.buf[ptc_grp->p[ptc_num].var.p_ep++] = idata;
}

//------------------------------------------------------------------------------
//   Protocol check & data atch fron Q Buffer
//------------------------------------------------------------------------------
void ptc_event (ptc_grp_t *ptc_grp, __u8 idata)
{
    __u8 ptc_pos;

dbg("%c\n", idata);
	for (ptc_pos = 0; ptc_pos < ptc_grp->pcnt; ptc_pos++)	{
		if (ptc_grp->p[ptc_pos].var.open)	{
			ptc_q (ptc_grp, ptc_pos, idata);
			if (ptc_grp->p[ptc_pos].pcheck (&ptc_grp->p[ptc_pos].var))	{
				if (ptc_grp->p[ptc_pos].pcatch (&ptc_grp->p[ptc_pos].var))	{
                    ptc_grp->p[ptc_pos].var.pass = true;
					ptc_set_status (ptc_grp, ptc_pos, false);
				}
			}
		}
	}
}

//------------------------------------------------------------------------------
//   UART Protocol Initiliaze Function
//------------------------------------------------------------------------------
__u8 ptc_func_init (ptc_grp_t *ptc_grp, __u8 ptc_num, __u8 ptc_size, 
    int (*chk_func)(ptc_var_t *var), int (*cat_func)(ptc_var_t *var), void *arg)
{
    ptc_grp->p[ptc_num].var.p_ep = 0;
    ptc_grp->p[ptc_num].var.p_sp = 0;
    ptc_grp->p[ptc_num].var.open = 1;
    ptc_grp->p[ptc_num].var.pass = 0;

    ptc_grp->p[ptc_num].var.size = ptc_size;
    ptc_grp->p[ptc_num].var.buf  = (__u8 *)(malloc(sizeof(__u8) * ptc_size));
    ptc_grp->p[ptc_num].var.arg  = arg;
    ptc_grp->p[ptc_num].pcheck   = chk_func;
    ptc_grp->p[ptc_num].pcatch   = cat_func;

    if ((cat_func == NULL) || (chk_func == NULL) ||
        (ptc_grp->p[ptc_num].var.buf == NULL))
        return 0;

    memset (ptc_grp->p[ptc_num].var.buf, 0x00, sizeof(__u8) * ptc_size);
    return 1;
}

//------------------------------------------------------------------------------
ptc_grp_t *ptc_grp_init (int fd, __u8 ptc_count)
{
    ptc_grp_t *ptc_grp = (ptc_grp_t *)(malloc(sizeof(ptc_grp_t)));

    if (ptc_grp != NULL) {
        memset (ptc_grp, 0x00, sizeof(ptc_grp_t));
        ptc_grp->fd         = fd;
        ptc_grp->pcnt       = ptc_count;
        ptc_grp->dq.sp      = 0;
        ptc_grp->dq.ep      = 0;
        ptc_grp->dq.size    = DEFAULT_QUEUE_SIZE;
        ptc_grp->dq.buf     = (__u8 *)(malloc(ptc_grp->dq.size));

        ptc_grp->p = (ptc_func_t *)(malloc(sizeof(ptc_func_t) * ptc_count));
        if (ptc_grp->p != NULL)
            memset (ptc_grp->p, 0x00, sizeof(ptc_func_t) * ptc_count);

        /* Create pthread for tx / rx */
        pthread_create(&ptc_grp->rx_thread, NULL, rx_thread_func, ptc_grp);
        pthread_create(&ptc_grp->tx_thread, NULL, tx_thread_func, ptc_grp);

        return ptc_grp;
    }
    return  0;
}

//------------------------------------------------------------------------------
void ptc_grp_close (ptc_grp_t *ptc_grp)
{
    __u8 ptc_pos;

    /* destroy pthread for tx / rx */
    {
        int ret;
        ret = pthread_detach(ptc_grp->rx_thread);
        dbg ("rx thread detach = %d\n", ret);
        ret = pthread_detach(ptc_grp->tx_thread);
        dbg ("tx thread detach = %d\n", ret);
    }

	for (ptc_pos = 0; ptc_pos < ptc_grp->pcnt; ptc_pos++)
        free (ptc_grp->p[ptc_pos].var.buf);

    free (ptc_grp->p);
    free (ptc_grp);
}
//------------------------------------------------------------------------------
//------------------------------------------------------------------------------
int uart_init (const char *dev_name, speed_t baud)
{
    int fd;
    unsigned char buf;
    // Create new termios struct, we call it 'tty' for convention
    struct termios tty;

    if ((fd = open(dev_name, O_RDWR)) < 0) {
        printf("%s open error!\n", dev_name);
        return -1;
    }

    // Read in existing settings, and handle any error
    if(tcgetattr(fd, &tty) != 0) {
        printf("Error %i from tcgetattr: %s\n", errno, strerror(errno));
        close(fd);
        return -1;
    }

    tty.c_cflag &= ~PARENB; // Clear parity bit, disabling parity (most common)
    tty.c_cflag &= ~CSTOPB; // Clear stop field, only one stop bit used in communication (most common)
    tty.c_cflag &= ~CSIZE; // Clear all bits that set the data size 
    tty.c_cflag |= CS8; // 8 bits per byte (most common)
    tty.c_cflag &= ~CRTSCTS; // Disable RTS/CTS hardware flow control (most common)
    tty.c_cflag |= CREAD | CLOCAL; // Turn on READ & ignore ctrl lines (CLOCAL = 1)

    tty.c_lflag &= ~ICANON;
    tty.c_lflag &= ~ECHO; // Disable echo
    tty.c_lflag &= ~ECHOE; // Disable erasure
    tty.c_lflag &= ~ECHONL; // Disable new-line echo
    tty.c_lflag &= ~ISIG; // Disable interpretation of INTR, QUIT and SUSP
    tty.c_iflag &= ~(IXON | IXOFF | IXANY); // Turn off s/w flow ctrl
    tty.c_iflag &= ~(IGNBRK|BRKINT|PARMRK|ISTRIP|INLCR|IGNCR|ICRNL); // Disable any special handling of received bytes

    tty.c_oflag &= ~OPOST; // Prevent special interpretation of output bytes (e.g. newline chars)
    tty.c_oflag &= ~ONLCR; // Prevent conversion of newline to carriage return/line feed
    // tty.c_oflag &= ~OXTABS; // Prevent conversion of tabs to spaces (NOT PRESENT ON LINUX)
    // tty.c_oflag &= ~ONOEOT; // Prevent removal of C-d chars (0x004) in output (NOT PRESENT ON LINUX)

    //tty.c_cc[VTIME] = 10;    // Wait for up to 1s (10 deciseconds), returning as soon as any data is received.
    tty.c_cc[VTIME] = 0;    
    tty.c_cc[VMIN] = 0;
    
    // Set in/out baud rate to be 9600
    cfsetispeed(&tty, baud);
    cfsetospeed(&tty, baud);

    // Save tty settings, also checking for error
    if (tcsetattr(fd, TCSANOW, &tty) != 0) {
        printf("Error %i from tcsetattr: %s\n", errno, strerror(errno));
        close(fd);
        return -1;
    }
    while(read(fd, &buf, 1));    // read all if there is data in the serial rx buffer

    return fd;
}

//------------------------------------------------------------------------------
void uart_close (int fd)
{
    if (fd)
        close(fd);
}

//------------------------------------------------------------------------------
//------------------------------------------------------------------------------
//------------------------------------------------------------------------------
