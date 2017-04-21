/* (c) 2016 Michael R. Tirado -- GPLv3, GNU General Public License version 3.
 *
 * spr16 client forked from xorg process
 *
 */
#define _GNU_SOURCE
#include <errno.h>
#include <stdio.h>
#include <memory.h>
#include <unistd.h>
#include <signal.h>
#include <sched.h>
#include <sys/prctl.h>
#include "../../../protocol/spr16.h"

#include <X11/keysym.h>
#include "xf86Xinput.h"
#include "input.h"
#include "inputstr.h"

#define STRERR strerror(errno)

struct spr16_msgdata_servinfo g_servinfo;
DeviceIntPtr g_inputdev;
int g_input_write;

void sporg_init()
{
	if (geteuid() == 0 || getuid() == 0) {
		/* TODO check for caps too */
		fprintf(stderr, "YOU ARE ROOT, defeating the entire purpose ");
		fprintf(stderr, "of this driver. don't do that!");
		/* TODO check env var if user wants to do this anyway */
		_exit(-1);
	}
	memset(&g_servinfo, 0, sizeof(g_servinfo));
	g_inputdev = NULL;
}

int handle_servinfo(struct spr16_msgdata_servinfo *sinfo)
{
	fprintf(stderr, "spr16 servinfo message received\n");
	fprintf(stderr, "width: %d\n", sinfo->width);
	fprintf(stderr, "height: %d\n", sinfo->height);
	fprintf(stderr, "bpp: %d\n",   sinfo->bpp);
	memcpy(&g_servinfo, sinfo, sizeof(g_servinfo));
	return 0;
}

struct spr16_msgdata_servinfo get_servinfo(char *srv_socket)
{
	int r;

	if (spr16_client_init()) {
		fprintf(stderr, "spr16_client_init()\n");
		goto err;
	}

	spr16_client_set_servinfo_handler(handle_servinfo);
	r = spr16_client_connect(srv_socket);
	if (r == -1) {
		fprintf(stderr, "spr16_client_connect(%s):\n", srv_socket);
		goto err;
	}

	if (spr16_client_update(5000)) {
		fprintf(stderr, "spr16_client_update()\n");
		goto err;
	}
	if (g_servinfo.width == 0) {
		fprintf(stderr, "servinfo timed out.\n");
		goto err;
	}

	spr16_client_shutdown();
	return g_servinfo;
err:
	spr16_client_shutdown();
	memset(&g_servinfo, 0, sizeof(g_servinfo));
	return g_servinfo;
}

uint16_t g_width, g_height;
int handle_servinfo_connect(struct spr16_msgdata_servinfo *sinfo)
{
	char name[SPR16_MAXNAME];
	snprintf(name, sizeof(name), "sporg-%d", getpid());
	if (g_servinfo.bpp != sinfo->bpp) {
		fprintf(stderr, "bpp mismatch \n");
		return -1;
	}
	if (g_width > g_servinfo.width || g_height > g_servinfo.height) {
		fprintf(stderr, "bad width/height %d/%d", g_width, g_height);
		return -1;
	}
	if (spr16_client_handshake_start(name, g_width, g_height, SPRITE_FLAG_INVERT_Y)){
		fprintf(stderr, "handshake_start failed\n");
		return -1;
	}
	return 0;
}

int handle_input(struct spr16_msgdata_input *input)
{
	int r;

again:
	r = write(g_input_write, input, sizeof(*input));
	if (r == -1) {
		if (errno == EAGAIN || errno == EINTR) {
			usleep(1000);
			goto again;
		}
		return -1;
	}
	return 0;
}

struct spr16 *spr16_connect(char *srv_socket, uint16_t width, uint16_t height)
{
	if (spr16_client_init())
		return NULL;

	g_width  = width;
	g_height = height;
	spr16_client_set_servinfo_handler(handle_servinfo_connect);
	spr16_client_set_input_handler(handle_input);
	if (spr16_client_connect(srv_socket) == -1) {
		fprintf(stderr, "connect(%s) error:\n", srv_socket);
		return NULL;
	}
	if (spr16_client_handshake_wait(10000)) {
		fprintf(stderr, "handshake failed\n");
		return NULL;
	}
	return spr16_client_get_sprite();
}

void client_main(pid_t xpid, int input_write)
{
	/* kill client when xorg exits. */
	prctl(PR_SET_PDEATHSIG, SIGKILL);
	g_input_write = input_write;

	while (1)
	{
		int r = spr16_client_update(-1);
		if (r == -1) {
			fprintf(stderr, "spr16 update error\n");
			break;
		}
	}
	spr16_client_shutdown();
	printf("killing %d\n", xpid);
	kill(xpid, SIGTERM);
	usleep(1000000);
	kill(xpid, SIGKILL);
}

int fork_client(int input_write)
{
	pid_t p, xpid;

	xpid = getpid();
	p = fork();
	if (p == 0) {
		client_main(xpid, input_write);
		_exit(-1);
	}
	else if (p == -1) {
		fprintf(stderr, "fork: %s\n", STRERR);
		_exit(-1);
	}
	return 0;
}


