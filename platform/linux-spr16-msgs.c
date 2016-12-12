/* (c) 2016 Michael R. Tirado -- GPLv3, GNU General Public License version 3.
 *
 * common functions used by client and server
 *
 *
 */
#define _GNU_SOURCE
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <sys/uio.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include "../protocol/spr16.h"

#define STRERR strerror(errno)

#define MAX_MSGBUF_READ (512 - SPRITE_MAXMSGLEN)
#define MIN_MSGBUF_READ (sizeof(struct spr16_msghdr)+2)
char g_msgbuf[MAX_MSGBUF_READ+SPRITE_MAXMSGLEN];
static void print_bytes(char *buf, const uint16_t len)
{
	int i;
	for (i = 0; i < len; ++i)
	{
		fprintf(stderr, "|%d", buf[i]);
	}
	fprintf(stderr, "\n");
}

uint32_t get_msghdr_typelen(struct spr16_msghdr *hdr)
{
	switch (hdr->type)
	{
	case SPRITEMSG_SERVINFO:
		return (uint32_t)sizeof(struct spr16_msgdata_servinfo);
	case SPRITEMSG_REGISTER_SPRITE:
		return (uint32_t)sizeof(struct spr16_msgdata_register_sprite);
	case SPRITEMSG_INPUT:
		return (uint32_t)sizeof(struct spr16_msgdata_input);
	case SPRITEMSG_ACK:
		return (uint32_t)sizeof(struct spr16_msgdata_ack);
	case SPRITEMSG_SYNC:
		return (uint32_t)sizeof(struct spr16_msgdata_sync);
	default:
		fprintf(stderr, "bad type(%d)\n", hdr->type);
		print_bytes(g_msgbuf, 32);
		return 0xffffffff;
	}
}
/* may return -1 with EAGAIN */
int spr16_write_msg(int fd, struct spr16_msghdr *hdr,
		void *msgdata, size_t msgdata_len)
{
	char msg[SPRITE_MAXMSGLEN];
	unsigned int intr_count = 0;
	hdr->id = 0;
	memcpy(msg, hdr, sizeof(*hdr));
	memcpy(msg + sizeof(*hdr), msgdata, msgdata_len);
interrupted:
	errno = 0;
	if (write(fd, msg, msgdata_len + sizeof(*hdr)) == -1) {
		if (errno == EINTR) {
			if (++intr_count > 1000) {
				errno = EAGAIN;
				return -1;
			}
			else {
				goto interrupted;
			}
		}
		return -1;
	}
	return 0;
}

/* space is reserved at end of read buffer to read a truncated message */
static int spr16_reassemble_fragment(int fd)
{
	char *fragpos;
	uint32_t typelen;
	int rdpos;
	int bytesleft = 0;
	int intr_count = 0;
	int fragbytes;
	int msglen;
	int r;

	/* find fragment */
	rdpos = 0;
	while (rdpos < MAX_MSGBUF_READ)
	{
		fragpos = g_msgbuf+rdpos;
		typelen = get_msghdr_typelen((struct spr16_msghdr *)fragpos);
		if (typelen > SPRITE_MAXMSGLEN - sizeof(struct spr16_msghdr)) {
			errno = EPROTO;
			return -1;
		}
		msglen = sizeof(struct spr16_msghdr) + typelen;
		rdpos += msglen;
	}

	/* size of fragment we already have */
	fragbytes = msglen - (rdpos - MAX_MSGBUF_READ);
	if (fragbytes == msglen) {
		return MAX_MSGBUF_READ; /* not actually truncated. */
	}
	else if (fragbytes < 2) {
		/* fragment == 1 byte, this shouldn't happen. if it does
		 * make sure read buffer + all structs divide by 2 evenly */
		fprintf(stderr, "check struct alignment.\n");
		errno = EPROTO;
		return -1;
	}

	/* continue with 2'nd read */
	bytesleft = msglen-fragbytes;
	if (bytesleft <= 0 || bytesleft+fragbytes > SPRITE_MAXMSGLEN) {
		errno = EPROTO;
		return -1;
	}
interrupted:
	r = read(fd, &g_msgbuf[MAX_MSGBUF_READ], bytesleft);
	if (r == -1 && errno == EINTR) {
		if (++intr_count < 100)
			goto interrupted;
		else
			return -1;
	}
	if (r != bytesleft) {
		errno = EPROTO;
		return -1;
	}
	return MAX_MSGBUF_READ+r;
}

char *spr16_read_msgs(int fd, uint32_t *outlen)
{
	int r;

	r = read(fd, g_msgbuf, MAX_MSGBUF_READ);
	if (r == -1) {
		return NULL;
	}
	if (r == MAX_MSGBUF_READ) {
		/* possibly truncated */
		r = spr16_reassemble_fragment(fd);
		if (r <= 0 ) {
			return NULL;
		}
	}

	if (r > (int)sizeof(g_msgbuf) || r < (int)MIN_MSGBUF_READ) {
		errno = EPROTO;
		return NULL;
	}
	*outlen = r;
	return g_msgbuf;
}

int spr16_send_ack(int fd, uint16_t ack, uint16_t ackinfo)
{
	struct spr16_msghdr hdr;
	struct spr16_msgdata_ack data;
	memset(&hdr, 0, sizeof(hdr));
	memset(&data, 0, sizeof(data));

	hdr.type = SPRITEMSG_ACK;
	data.ack = ack;
	data.info = ackinfo;
	if (spr16_write_msg(fd, &hdr, &data, sizeof(data))) {
		return -1;
	}
	return 0;
}

int spr16_dispatch_msgs(int fd, char *msgbuf, uint32_t buflen)
{
	struct spr16_msghdr *msghdr;
	char *msgpos, *msgdata;
	int rdpos;
	uint32_t typelen;
	errno = 0;
	rdpos = 0;
	while (rdpos+sizeof(struct spr16_msghdr) < buflen)
	{
		msgpos  = msgbuf+rdpos;
		msghdr  = (struct spr16_msghdr *)msgpos;
		msgdata = msgpos+sizeof(struct spr16_msghdr);
		typelen = get_msghdr_typelen(msghdr);
		if (typelen > SPRITE_MAXMSGLEN - sizeof(struct spr16_msghdr)
				|| msgdata+typelen > msgbuf+buflen) {
			errno = EPROTO;
			return -1;
		}

#ifdef SPR16_SERVER
		switch (msghdr->type)
		{
		case SPRITEMSG_SERVINFO:
			if (spr16_server_servinfo(fd)) {
				fprintf(stderr, "servinfo failed\n");
				return -1;
			}
			break;
		case SPRITEMSG_REGISTER_SPRITE:
			if (spr16_server_register_sprite(fd,
					(struct spr16_msgdata_register_sprite *)
					msgdata)) {
				fprintf(stderr, "register failed\n");
				return -1;
			}
			break;
		case SPRITEMSG_SYNC:
			if (spr16_server_sync((struct spr16_msgdata_sync *)msgdata)) {
				fprintf(stderr, "sync failed\n");
				return -1;
			}
			break;
		default:
			fprintf(stderr, "unknown msg type\n");
			errno = EPROTO;
			return -1;
		}
#else /* CLIENT */
		(void)fd;
		switch (msghdr->type)
		{
		case SPRITEMSG_SERVINFO:
			if (spr16_client_servinfo(
					(struct spr16_msgdata_servinfo *)msgdata)) {
				fprintf(stderr, "servinfo failed\n");
				return -1;
			}
			break;
		case SPRITEMSG_ACK:
			if (spr16_client_ack((struct spr16_msgdata_ack *)msgdata)) {
				fprintf(stderr, "ack failed\n");
				return -1;
			}
			break;
		case SPRITEMSG_INPUT:
			if (spr16_client_input((struct spr16_msgdata_input *)msgdata)) {
				fprintf(stderr, "input failed\n");
				return -1;
			}
			break;
		default:
			errno = EPROTO;
			return -1;
		}
#endif
		rdpos += sizeof(struct spr16_msghdr) + typelen;
	}

	return 0;
}

int afunix_send_fd(int sock, int fd)
{
	union {
		struct cmsghdr cmh;
		char control[CMSG_SPACE(sizeof(int))];
	} control_un;
	struct cmsghdr *cmhp;
	struct msghdr msgh;
	struct iovec iov;
	int  retval;
	char data = 'F';
	int  i;

	if (sock == -1 || fd == -1) {
		fprintf(stderr, "invalid descriptor\n");
		return -1;
	}

	memset(&msgh, 0, sizeof(msgh));
	msgh.msg_iov = &iov;
	msgh.msg_iovlen = 1;
	msgh.msg_name = NULL;
	msgh.msg_namelen = 0;
	msgh.msg_control = control_un.control;
	msgh.msg_controllen = sizeof(control_un.control);

	iov.iov_base = &data;
	iov.iov_len = sizeof(data);

	cmhp = CMSG_FIRSTHDR(&msgh);
	cmhp->cmsg_len = CMSG_LEN(sizeof(int));
	cmhp->cmsg_level = SOL_SOCKET;
	cmhp->cmsg_type = SCM_RIGHTS;
	*((int *)CMSG_DATA(cmhp)) = fd;
	for (i = 0; i < 1000; ++i) {
		retval = sendmsg(sock, &msgh, MSG_DONTWAIT);
		if (retval == -1 && errno == EINTR) {
			continue;
		}
		else {
			break;
		}
	}

	if (retval != (int)iov.iov_len){
		fprintf(stderr, "sendmsg returned: %d\n", retval);
		if (retval == -1)
			fprintf(stderr, "sendmsg error(%d): %s\n",
					retval, strerror(errno));
		return -1;
	}

	return 0;
}



int afunix_recv_fd(int sock, int *fd_out)
{
	union {
		struct cmsghdr cmh;
		char control[CMSG_SPACE(sizeof(int))];
	} control_un;
	struct cmsghdr *cmhp;
	struct msghdr msgh;
	struct iovec iov;
	char data;
	int fd;
	int retval;

	errno = 0;
	if (fd_out == NULL)
		return -1;
	*fd_out = -1;

	memset(&control_un, 0, sizeof(control_un));
	control_un.cmh.cmsg_len = CMSG_LEN(sizeof(int));
	control_un.cmh.cmsg_level = SOL_SOCKET;
	control_un.cmh.cmsg_type = SCM_RIGHTS;

	memset(&msgh, 0, sizeof(msgh));
	msgh.msg_control = control_un.control;
	msgh.msg_controllen = sizeof(control_un.control);
	msgh.msg_name = NULL;
	msgh.msg_namelen = 0;
	msgh.msg_iov = &iov;
	msgh.msg_iovlen = 1;

	iov.iov_base = &data;
	iov.iov_len = sizeof(data);

	retval = recvmsg(sock, &msgh, MSG_DONTWAIT);
	if (retval == -1 && (errno == EAGAIN || errno == EINTR)) {
		return -1;
	}
	else if (retval == 0 || retval == -1 ) {
		if (retval == 0)
			errno = ECONNRESET;
		return -1;
	}
	cmhp = CMSG_FIRSTHDR(&msgh);
	if (cmhp == NULL) {
		fprintf(stderr, "recv_fd error, no message header\n");
		return -1;
	}
	if ( cmhp->cmsg_len != CMSG_LEN(sizeof(int))) {
		fprintf(stderr, "cmhp(%p)\n", (void *)cmhp);
		fprintf(stderr, "bad cmsg header / message length\n");
		return -1;
	}
	if (cmhp->cmsg_level != SOL_SOCKET) {
		fprintf(stderr, "cmsg_level != SOL_SOCKET");
		return -1;
	}
	if (cmhp->cmsg_type != SCM_RIGHTS) {
		fprintf(stderr, "cmsg_type != SCM_RIGHTS");
		return -1;
	}

	fd = *((int *) CMSG_DATA(cmhp));
	if (data != 'F') {
		fprintf(stderr, "received an improper file, closing.\n");
		close(fd);
		return -1;
	}
	*fd_out = fd;
	return 0;
}



