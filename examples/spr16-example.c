/* (c) 2016 Michael R. Tirado -- GPLv3, GNU General Public License version 3.
 *
 */
#define _GNU_SOURCE
#include <errno.h>
#include <stdio.h>
#include <sys/epoll.h>
#include <memory.h>
#include <unistd.h>
#include "../protocol/spr16.h"
#include "game.h"

#define STRERR strerror(errno)

int handle_servinfo(struct spr16_msgdata_servinfo *sinfo)
{
	printf("client: read msg\n");
	printf("max width: %d\n", sinfo->width);
	printf("max width: %d\n", sinfo->height);
	printf("max bpp: %d\n",   sinfo->bpp);
	if (spr16_client_handshake_start("Landit", 640, 480))
		return -1;
	return 0;
}

int main(int argc, char *argv[])
{
	struct spr16 *screen;
	int r;

	if (argc < 2) {
		printf("usage: program <server-name>\n");
		printf("e.g: spr16_example tty1\n");
		return -1;
	}
	/* line buffer output */
	setvbuf(stdout, NULL, _IOLBF, 0);
	setvbuf(stderr, NULL, _IOLBF, 0);

	if (spr16_client_init())
		return -1;
	spr16_client_set_servinfo_handler(handle_servinfo);
	spr16_client_set_input_handler(game_input);

	r = spr16_client_connect(argv[1]);
	if (r == -1) {
		printf("connect(%s) error:\n", argv[1]);
		return -1;
	}

	if (spr16_client_handshake_wait(10000)) {
		printf("handshake timed out\n");
		return -1;
	}
	screen = spr16_client_get_sprite();
	if (!screen)
		return -1;
	if (game_init(screen))
		return -1;
	while (1)
	{
		r = spr16_client_update();
		if (r == -1) {
			printf("spr16 update error\n");
			return -1;
		}
		if (game_update())
			return -1;
		usleep(8000); /* reduce single buffer tearing */
		if (game_draw())
			return -1;
	}
	spr16_client_shutdown();

	return 0;
}



