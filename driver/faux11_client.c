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
#include "../protocol/spr16.h"

#include <X11/keysym.h>
#include "xf86Xinput.h"
#include "input.h"
#include "inputstr.h"

#define STRERR strerror(errno)

struct spr16_msgdata_servinfo g_servinfo;
DeviceIntPtr g_inputdev;
#define STKSIZE (256*1024)
char *g_clonestack;

void faux11_init()
{
	if (geteuid() == 0 || getuid() == 0) {
		/* TODO check for caps too */
		fprintf(stderr, "YOU ARE ROOT, defeating the whole point ");
		fprintf(stderr, "of this driver. don't do that!");
		_exit(-1);
	}
	memset(&g_servinfo, 0, sizeof(g_servinfo));
	g_inputdev = NULL;
	g_clonestack = NULL;
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
	char name[SPRITE_MAXNAME];
	snprintf(name, sizeof(name), "faux11-%d", getpid());
	if (g_servinfo.bpp != sinfo->bpp) {
		fprintf(stderr, "bpp mismatch \n");
		return -1;
	}
	if (g_width > g_servinfo.width || g_height > g_servinfo.height) {
		fprintf(stderr, "bad width/height %d/%d", g_width, g_height);
		return -1;
	}
	if (spr16_client_handshake_start(name, g_width, g_height)) {
		fprintf(stderr, "handshake failed\n");
		return -1;
	}
	return 0;
}

static DeviceIntPtr find_keyboard()
{
	const char kfind[] = "faux11input";
	InputInfoPtr pInfo;
	DeviceIntPtr dev = inputInfo.devices;
	for ( ; dev; dev = dev->next)
	{
		pInfo = dev->public.devicePrivate;
		if (!pInfo) {
			continue;
		}
		if (strcmp(kfind, pInfo->type_name) == 0) {
			fprintf(stderr, "kbd name: %s\n", dev->name);
			return dev;
		}
	}
	return NULL;
}

int handle_input(uint16_t flags, uint16_t keycode)
{
	InputInfoPtr pInfo;
	struct spr16_msgdata_input_keyboard kbmsg;
	kbmsg.flags = flags;
	kbmsg.keycode = keycode;

	if (g_inputdev == NULL) {
		g_inputdev = find_keyboard();
		if (g_inputdev == NULL) {
			fprintf(stderr, "no keyboard device found\n");
			return 0; /* 	this could fail, race until input driver loads
					remove input driver completely if possible
					or just deal with this. */
		}
	}
	pInfo = g_inputdev->public.devicePrivate;
	write((int)pInfo->private, &kbmsg, sizeof(kbmsg));

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

int client_main(pid_t xpid)
{
	int r = 0;
	/* kill client when xorg exits. */
	prctl(PR_SET_PDEATHSIG, SIGKILL);
	while (1)
	{
		r = spr16_client_update(-1);
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
	return r;
}

int clone_func(void *v)
{
	pid_t xpid = *(pid_t *)v;
	_exit(client_main(xpid));
}

int fork_client()
{
	char *topstack;
	pid_t p, xpid;

	xpid = getpid();
	if (g_clonestack == NULL) {
		g_clonestack = malloc(STKSIZE);
		if (g_clonestack == NULL) {
			return -1;
		}
	}
	else {
		fprintf(stderr, "stack already allocated?\n");
		return 0;
	}
	topstack = g_clonestack + STKSIZE;
	p = clone(clone_func, topstack, CLONE_VM|CLONE_FILES|SIGCHLD, &xpid);
	if (p == -1) {
		fprintf(stderr, "fork: %s\n", STRERR);
	}
	return 0;
}


