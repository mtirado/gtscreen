/* (c) 2017 Michael R. Tirado -- GPLv3+, GNU General Public License version 3 or later
 *
 * TODO optimize client lookups
 *
 */
#include <memory.h>
#include "protocol/spr16.h"
#include "screen.h"

int screen_init(struct screen *self)
{
	memset(self, 0, sizeof(struct screen));
	return 0;
}

int screen_add_client(struct screen *self, struct client *cl)
{
	if (self->clients)
		return -1;
	self->clients = cl;
	self->focused_client = cl;
	return 0;
}

struct client *screen_find_client(struct screen *self, int fd)
{
	struct client *cl;
	cl = self->clients;
	while (cl)
	{
		if (cl->socket == fd)
			return cl;
		cl = cl->next;
	}
	return NULL;
}

struct client *screen_remove_client(struct screen *self, int fd)
{
	struct client **trail, *cl;
	trail = &self->clients;
	cl    =  self->clients;
	while (cl)
	{
		if (cl->socket == fd) {
			if (*trail == self->clients) {
				/* lone client */
				self->clients = NULL;
				self->focused_client = NULL;
				return cl;
			}
			else if (cl == self->focused_client) {
				self->focused_client = NULL;
			}
			*trail = cl->next;
			return cl;
		}
		trail = &cl->next;
		cl = cl->next;
	}
	return NULL;
}
