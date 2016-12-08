/* (c) 2016 Michael R. Tirado -- GPLv3, GNU General Public License version 3.
 *
 */

#ifndef SPR16_H__
#define SPR16_H__

#include <stdint.h>
#include <stddef.h>
/*
 * This is a suuuuper simple display protocol.
 * buffer id=0 should be invalid,
 *
 * one client = one shared mem region, that's it!
 * memory is mapped as part of connection handshake.
 * resizing is not supported.
 *
 * TODO currently it assumes 32bpp ARGB
 *
 * for simplicity sake, registered buffers cannot be shrunk or increased.
 * this may be possible eventually but would require a buffer remap, or sync
 * over pipes instead of shmem. there may be platform specific options to
 * improve this, but i have not gone through DRM thoroughly enough to get
 * a clear picture of how the protocol would look while still supporting
 * complete software-only operation, so this protocol is obviously
 * not set in stone..
 *
 * this is not suitible for networked usage, due to the nature of
 * struct padding on various architectures. you would want to add some sync
 * compression in addition to proper serialization if taking that route.
 *
 */

/* XXX NOTE: all structs must be evenly sized / divide evenly by 2, unless you
 * want to handle adding a triple read when msg truncates with 1 byte fragment
 */

#define SPRITE_MAXMSGLEN 64 /* hdr+data */
#define SPRITE_MAXNAME   32
#define SPRITE_SOCKPATH "/tmp/spr16"
#define SPRITE_LOGPATH "/usr/var/log/spr16"
#define SPRITE_ACK  1
#define SPRITE_NACK 0

/* TODO set sticky bit and check that, + perms like xephyr does ;)
 *
 * */


/* ack info */
enum {
	SPRITEACK_SEND_DESCRIPTOR=1,
	SPRITEACK_ESTABLISHED,
	SPRITENACK_SIZE, /* size might need to be padded beyond w*h*bpp */
	SPRITENACK_WIDTH,
	SPRITENACK_HEIGHT,
	SPRITENACK_BPP,
	SPRITENACK_SHMEM,
	SPRITENACK_DISCONNECT
};

/* msg types
 *
 * SERVINFO        - Server sends global parameters to client.
 * REGISTER_SPRITE - This message is handled only once to complete handshake.
 * 		     maps shared memory between server and client.
 * INPUT           - Send input event.
 * ACK             - ACK or NACK message.
 * SYNC            - Notify server of modified buffer region.
 *
 * */
enum {
	SPRITEMSG_SERVINFO=123,
	SPRITEMSG_REGISTER_SPRITE,
	SPRITEMSG_INPUT,
	SPRITEMSG_ACK,
	SPRITEMSG_SYNC
};

/* bit flags */
#define SPRITE_VISIBLE 1

struct spr16_shmem {
	char *addr;
	int fd;
	uint32_t size;
};

/* sprite object */
struct spr16 {
	char name[SPRITE_MAXNAME];
	struct spr16_shmem shmem;
	uint32_t flags;
	int16_t  x;
	int16_t  y;
	int16_t  z;
	uint16_t width;
	uint16_t height;
	uint16_t bpp;
};

/*
 * the msghdr is immediately followed by specific msgdata struct
 * these two structs should be written in the same write call
 * with no possibility of truncated writes.
 */
struct spr16_msghdr {
	uint16_t type;
	uint16_t id;
	/* if you add to this be warned, linux platform code assumes this struct is only
	 * 2 uint16_t's, in a few places. make sure you know what you're doing
	 */
};

struct spr16_msgdata_servinfo {
	uint16_t width;
	uint16_t height;
	uint16_t bpp;
};

/* this message is immediately followed by 1 byte SCM_RIGHTS message
 * to transfer sprite file descriptor to server process */
struct spr16_msgdata_register_sprite {
	char name[SPRITE_MAXNAME];
	uint16_t width;
	uint16_t height;
	uint16_t bpp;
	/* TODO surface formats, just using drm default 32bit ARGB */
};

/* can be ack or nack, with some info */
struct spr16_msgdata_ack {
	uint16_t info;
	uint16_t ack; /* 0 == nack */
};

/* client requesting synchronization */
struct spr16_msgdata_sync {
	uint16_t x;
	uint16_t y;
	uint16_t width;
	uint16_t height;
};


struct spr16_msgdata_input {
	uint32_t val;
	uint16_t code;
	uint8_t  type;
	uint8_t  bits;
};

enum {
	SPR16_INPUT_KEY = 1,   /* full spr16 keycodes */
	SPR16_INPUT_KEY_ASCII, /* fallback ascii mapped keycodes */
	SPR16_INPUT_AXIS_RELATIVE,
	SPR16_INPUT_AXIS_ABSOLUTE,
	SPR16_INPUT_NOTICE
};

enum {
	SPR16_NOTICE_INPUT_FLUSH = 1
};
/* TODO implement tracking id's, and should probably make bits type specific,
 * for better flexibility. ascii can't be raw, no tracking id, etc */
#define SPR16_INPUT_TRACK_MASK 0x1f /* 5 bit tracking id */
#define SPR16_INPUT_FLAGS_MASK 0x70 /* 3 bit flags */
#define SPR16_INPUT_FLAG_RAW   0x20 /* raw bytes, application specific */
/* 0x40, 0x80 */

enum {
	SPR16_KEYCODE_LSHIFT = 0x0100, /* beginning of non-ascii values */
	SPR16_KEYCODE_RSHIFT,
	SPR16_KEYCODE_LCTRL,
	SPR16_KEYCODE_RCTRL,
	SPR16_KEYCODE_LALT,
	SPR16_KEYCODE_RALT,
	SPR16_KEYCODE_UP,
	SPR16_KEYCODE_CAPSLOCK,
	SPR16_KEYCODE_DOWN,
	SPR16_KEYCODE_LEFT,
	SPR16_KEYCODE_RIGHT,
	SPR16_KEYCODE_PAGEUP,
	SPR16_KEYCODE_PAGEDOWN,
	SPR16_KEYCODE_HOME,
	SPR16_KEYCODE_END,
	SPR16_KEYCODE_INSERT,
	SPR16_KEYCODE_DELETE,
	SPR16_KEYCODE_F1,
	SPR16_KEYCODE_F2,
	SPR16_KEYCODE_F3,
	SPR16_KEYCODE_F4,
	SPR16_KEYCODE_F5,
	SPR16_KEYCODE_F6,
	SPR16_KEYCODE_F7,
	SPR16_KEYCODE_F8,
	SPR16_KEYCODE_F9,
	SPR16_KEYCODE_F10,
	SPR16_KEYCODE_F11,
	SPR16_KEYCODE_F12,
	SPR16_KEYCODE_F13,
	SPR16_KEYCODE_F14,
	SPR16_KEYCODE_F15,
	SPR16_KEYCODE_F16,
	SPR16_KEYCODE_F17,
	SPR16_KEYCODE_F18,
	SPR16_KEYCODE_F19,
	SPR16_KEYCODE_F20,
	SPR16_KEYCODE_F21,
	SPR16_KEYCODE_F22,
	SPR16_KEYCODE_F23,
	SPR16_KEYCODE_F24,
	SPR16_KEYCODE_LBTN,
	SPR16_KEYCODE_RBTN,
	SPR16_KEYCODE_ABTN,
	SPR16_KEYCODE_BBTN,
	SPR16_KEYCODE_CBTN,
	SPR16_KEYCODE_DBTN,
	SPR16_KEYCODE_EBTN,
	SPR16_KEYCODE_FBTN
};

typedef int (*input_handler)(struct spr16_msgdata_input *input);
typedef int (*servinfo_handler)(struct spr16_msgdata_servinfo *sinfo);

/*----------------------------------------------*
 * message handling                             *
 *----------------------------------------------*/
char *spr16_read_msgs(int fd, uint32_t *outlen);
int spr16_dispatch_msgs(int fd, char *msgbuf, uint32_t buflen);
int spr16_write_msg(int fd, struct spr16_msghdr *hdr,
                    void *msgdata, size_t msgdata_len);
int spr16_send_ack(int fd, uint16_t ack, uint16_t ackinfo);
int afunix_send_fd(int sock, int fd);
int afunix_recv_fd(int sock, int *fd_out);


/*----------------------------------------------*
 * client side                                  *
 *----------------------------------------------*/
int spr16_client_init();
int spr16_client_connect(char *name);
int spr16_client_handshake_start(char *name, uint16_t width, uint16_t height);
int spr16_client_handshake_wait(uint32_t timeout);
int spr16_client_servinfo(struct spr16_msgdata_servinfo *sinfo);
/* TODO pixel formats */
int spr16_client_register_sprite(char *name, uint16_t width, uint16_t height);
int spr16_client_update(int poll_timeout); /* timeout in milliseconds, <0 blocks */
int spr16_client_shutdown();
struct spr16_msgdata_servinfo *spr16_client_get_servinfo();
int spr16_client_ack(struct spr16_msgdata_ack *ack);
struct spr16 *spr16_client_get_sprite();
/* may return -1 with errno set to EAGAIN, if server buffer is full */
int spr16_client_sync(uint16_t x, uint16_t y, uint16_t width, uint16_t height);
int spr16_client_input(struct spr16_msgdata_input *msg);
int spr16_client_set_input_handler(input_handler func);
int spr16_client_set_servinfo_handler(servinfo_handler func);


/*----------------------------------------------*
 * server side                                  *
 *----------------------------------------------*/
int spr16_server_init(uint16_t width, uint16_t height, uint16_t bpp);
int spr16_server_servinfo(int fd);
int spr16_server_sync(struct spr16_msgdata_sync *region);
int spr16_server_register_sprite(int fd, struct spr16_msgdata_register_sprite *reg);
int spr16_server_update();
int spr16_open_memfd(struct spr16_msgdata_register_sprite *reg);
int spr16_server_init_input();
int spr16_server_shutdown(int listen_fd);



/*
 * Simple usage scenario:
 *
 * client                              server
 *   |                                    |
 *   |                                    init(tty1)
 *   connect -------------------------->  |
 *   |  <-------------------------------- servinfo
 *   register_buffer ------------------>  |
 *   send_descriptor ------------------>  |
 *   |  <-------------------------------- ack(ESTABLISHED)/nack()
 *   sync_region(id,x,y,w,h) ---------->  |
 *   sync_region(id,x,y,w,h) ---------->  |
 *   |  <-------------------------------- nack
 *   disconnect ------------------------> X
 */



/*----------------------------------------------*
 * input drivers                                *
 *----------------------------------------------*/
struct input_device;
typedef int (*input_read)(struct input_device *self, int client);
typedef int (*input_flush)(struct input_device *self);
struct input_device {
	char path[128];
	char name[64];
	struct input_device *next;
	input_read  func_read;
	input_flush func_flush;
	uint32_t private;
	int fd;
};

#endif
