#########################################
# global defines
#########################################
ifndef DESTDIR
DESTDIR=/usr/local
endif

# if you have a 64bit system set bitcount(TODO untested).
DEFINES := -DMAX_SYSTEMPATH=1024 -DPTRBITCOUNT=32
CFLAGS  := -pedantic -Wall -Wextra -Werror $(DEFINES)
DEFLANG := -ansi
#DBG	:= -g -march=core2 -mtune=core2
# TODO benchmark sync

#########################################
# objects
#########################################
GTSCREEN_SRCS := ./main.c			\
		 ./screen.c			\
		 ./platform/linux-drm.c		\
		 ./platform/linux-vt.c		\
		 ./platform/linux-input.c	\
		 ./platform/linux-spr16-msgs.c	\
		 ./platform/linux-spr16-server.c

GTSCREEN_OBJS := $(GTSCREEN_SRCS:.c=.gtscreen.o) \
		 ./platform/x86.asm.o
# example program
SPR16_EX_SRCS := ./examples/spr16-example.c	\
		 ./examples/game.c		\
		 ./examples/util.c		\
		 ./examples/dynamics.c		\
		 ./examples/moon.c		\
		 ./examples/craft.c		\
		 ./platform/linux-spr16-msgs.c	\
		 ./platform/linux-spr16-client.c
SPR16_EX_OBJS := $(SPR16_EX_SRCS:.c=.spr16_ex.o)

# touch input example
TOUCHPAINT_SRCS := ./examples/touchpaint.c	\
		 ./examples/util.c		\
		 ./platform/linux-spr16-msgs.c	\
		 ./platform/linux-spr16-client.c
TOUCHPAINT_OBJS := $(TOUCHPAINT_SRCS:.c=.touchpaint.o)


#  spr16-x11-xorg graphic drivers
SPORG_GFX_SRCS := 	./airlock/driver/x11/sporg.c		\
			./airlock/driver/x11/sporg_client.c	\
			./platform/linux-spr16-msgs.c		\
			./platform/linux-spr16-client.c
SPORG_GFX_OBJS := $(SPORG_GFX_SRCS:.c=.sporg_gfx.o)
SPORG_GFX_INC := -I/usr/include/xorg -I/usr/include/pixman-1

#  spr16-x11-xorg input driver
SPORG_INPUT_SRCS := ./airlock/driver/x11/sporg_input.c
SPORG_INPUT_OBJS := $(SPORG_INPUT_SRCS:.c=.sporg_input.o)
SPORG_INPUT_INC := -I/usr/include/xorg -I/usr/include/pixman-1

########################################
# target files
########################################
GTSCREEN := gtscreen
SPR16_EX := spr16_example
TOUCHPAINT := touchpaint
SPORG_GFX   := sporg_drv.so
SPORG_INPUT := sporginput_drv.so


########################################
# build
########################################
#%.o: %.c
#	$(CC) -c $(DEFLANG) $(CFLAGS) $(DBG) -o $@ $<
%.asm.o: %.s
	$(AS) -o $@ $<
%.gtscreen.o: %.c
	$(CC) -c $(DEFLANG) -DSPR16_SERVER $(CFLAGS) $(DBG) -o $@ $<
%.spr16_ex.o: %.c
	$(CC) -c $(DEFLANG) $(CFLAGS) $(DBG) -o $@ $<
%.touchpaint.o: %.c
	$(CC) -c $(DEFLANG) $(CFLAGS) $(DBG) -o $@ $<
%.sporg_gfx.o: %.c
	$(CC) -c -std=gnu99 -pedantic -Wall -fPIC $(DBG) $(SPORG_GFX_INC) -o $@ $<
%.sporg_input.o: %.c
	$(CC) -c -std=gnu99 -pedantic -Wall -fPIC $(DBG) $(SPORG_INPUT_INC) -o $@ $<
all:			\
	$(GTSCREEN)	\
	$(SPR16_EX)	\
	$(TOUCHPAINT)	\
	$(SPORG_GFX)	\
	$(SPORG_INPUT)

########################################
# targets
########################################

# XXX -lm for floating point is only by gtscreen to calculate relative acceleration
# and to convert absoulute touch devices to relative trackpads.
# this can be ifdef'd out or done using some integer technique for systsems without fpu
$(GTSCREEN):		$(GTSCREEN_OBJS)
			$(CC) $(DEFINES) -lm $(LDFLAGS) $(GTSCREEN_OBJS) -o $@
			@echo ""
			@echo "x---------------------x"
			@echo "| gtscreen         OK |"
			@echo "x---------------------x"
			@echo ""

$(SPR16_EX):		$(SPR16_EX_OBJS)
			$(CC) $(LDFLAGS) -lm $(SPR16_EX_OBJS) -o $@
			@echo ""
			@echo "x---------------------x"
			@echo "| spr16_example    OK |"
			@echo "x---------------------x"
			@echo ""

$(TOUCHPAINT):		$(TOUCHPAINT_OBJS)
			$(CC) $(LDFLAGS) -lm $(TOUCHPAINT_OBJS) -o $@
			@echo ""
			@echo "x---------------------x"
			@echo "| touchpaint       OK |"
			@echo "x---------------------x"
			@echo ""

$(SPORG_GFX):		$(SPORG_GFX_OBJS)
			$(CC) $(LDFLAGS) -shared $(SPORG_GFX_OBJS) -o $@
			@echo ""
			@echo "x---------------------x"
			@echo "| sporg_gfx        OK |"
			@echo "x---------------------x"
			@echo ""

$(SPORG_INPUT):	$(SPORG_INPUT_OBJS)
			$(CC) $(LDFLAGS) -shared -lX11 $(SPORG_INPUT_OBJS) -o $@
			@echo ""
			@echo "x---------------------x"
			@echo "| sporg_input      OK |"
			@echo "x---------------------x"
			@echo ""

install:
	@umask 022
	@install -dvm 0755  "$(DESTDIR)/bin"
	@install -dvm 0755  "$(DESTDIR)/lib/xorg/modules/drivers"
	@install -dvm 0755  "$(DESTDIR)/lib/xorg/modules/input"
	@install -Dvm 0755  "$(GTSCREEN)"    "$(DESTDIR)/bin/$(GTSCREEN)"
	@install -Dvm 0755  "$(TOUCHPAINT)"  "$(DESTDIR)/bin/$(TOUCHPAINT)"
	@install -Dvm 0755  "$(SPR16_EX)"    "$(DESTDIR)/bin/$(SPR16_EX)"
	@install -Dvm 0755  "$(SPORG_GFX)"   \
		"$(DESTDIR)/lib/xorg/modules/drivers/$(SPORG_GFX)"
	@install -Dvm 0755  "$(SPORG_INPUT)" \
		"$(DESTDIR)/lib/xorg/modules/input/$(SPORG_INPUT)"




clean:
	@$(foreach obj, $(GTSCREEN_OBJS), rm -fv $(obj);)
	@$(foreach obj, $(SPR16_EX_OBJS), rm -fv $(obj);)
	@$(foreach obj, $(TOUCHPAINT_OBJS), rm -fv $(obj);)
	@$(foreach obj, $(SPORG_GFX_OBJS), rm -fv $(obj);)
	@$(foreach obj, $(SPORG_INPUT_OBJS), rm -fv $(obj);)

	@-rm -fv ./$(GTSCREEN)
	@-rm -fv ./$(SPR16_EX)
	@-rm -fv ./$(TOUCHPAINT)
	@-rm -fv ./$(SPORG_GFX)
	@-rm -fv ./$(SPORG_INPUT)
	@echo "cleaned."

