/* (c) 2017 Michael R. Tirado -- GPLv3+, GNU General Public License version 3 or later
 *
 * TODO currently only does 1 sprite per screen, need to add good stuff like
 *  sprite layouts, tiling, splits, whatever you call em, which means we need
 *  sprite resizing OR a mode that dictates sprite sizes and sets up static screens.
 *  some sprites need staticly size flag anyway for e.g: x11 encapsulation...
 *
 *  maybe query server for current screen real estate, and work out from there.
 */
#ifndef SCREEN_H__
#define SCREEN_H__
#include "protocol/spr16.h"

int screen_init(struct screen *self);
int screen_add_client(struct screen *self, struct client *cl);
struct client *screen_find_client(struct screen *self, int fd);
struct client *screen_remove_client(struct screen *self, int socket_fd);
int screen_free(struct screen *self);

#endif
