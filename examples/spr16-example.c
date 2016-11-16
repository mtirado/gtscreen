/* (c) 2016 Michael R. Tirado -- GPLv3, GNU General Public License version 3.
 *
 */
#define _GNU_SOURCE
#include <errno.h>
#include <stdio.h>
#include <sys/epoll.h>
#include <memory.h>
#include <unistd.h>
#include <time.h>
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
	struct timespec tcur, tlast;
	int r;

	if (argc < 2) {
		printf("usage: program <server-name>\n");
		printf("e.g: spr16_example tty1\n");
		return -1;
	}

	/* line buffer output */
	setvbuf(stdout, NULL, _IOLBF, 0);
	setvbuf(stderr, NULL, _IOLBF, 0);

	if (clock_gettime(CLOCK_MONOTONIC_RAW, &tlast))
		return -1;
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
		unsigned int usec;
		struct timespec elapsed;
		r = spr16_client_update();
		if (r == -1) {
			printf("spr16 update error\n");
			return -1;
		}
		if (game_update())
			return -1;

		/* rate limit syncs, don't nuke the server */
		if (clock_gettime(CLOCK_MONOTONIC_RAW, &tcur))
			return -1;
		elapsed.tv_sec  = tcur.tv_sec - tlast.tv_sec;
		if (!elapsed.tv_sec) {
			elapsed.tv_nsec = tcur.tv_nsec - tlast.tv_nsec;
			usec = elapsed.tv_nsec / 1000;
		}
		else {
			usec = ((1000000000 - tlast.tv_nsec) + tcur.tv_nsec) / 1000;
			usec += (elapsed.tv_sec-1) * 1000000;
		}
		if (usec >= 32000) { /* roughly 30fps */
			tlast = tcur;
			if (game_draw())
				return -1;
		}
		/* spare client cpu usage */
		usleep(500);
	}
	spr16_client_shutdown();

	return 0;
}



