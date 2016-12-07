#########################################
# global defines
#########################################
# if you have a 64bit system set bitcount(TODO untested).
DEFINES := -DMAX_SYSTEMPATH=1024 -DPTRBITCOUNT=32
CFLAGS  := -pedantic -Wall -Wextra -Werror $(DEFINES)
DEFLANG := -ansi
DBG	:= -g

#########################################
# optional features
#########################################


#TODO strip debugging info
#########################################
# objects
#########################################
GTSCREEN_SRCS := ./main.c			\
		 ./platform/linux-drm.c		\
		 ./platform/linux-vt.c		\
		 ./platform/linux-input.c	\
		 ./platform/linux-spr16-msgs.c	\
		 ./platform/linux-spr16-server.c

GTSCREEN_OBJS := $(GTSCREEN_SRCS:.c=.gtscreen.o)

SPR16_EX_SRCS := ./examples/spr16-example.c	\
		 ./examples/game.c		\
		 ./examples/dynamics.c		\
		 ./examples/moon.c		\
		 ./examples/craft.c		\
		 ./platform/linux-spr16-msgs.c	\
		 ./platform/linux-spr16-client.c
SPR16_EX_OBJS := $(SPR16_EX_SRCS:.c=.spr16_ex.o)

FAUX11_GFX_SRCS :=	 ./driver/faux11.c		\
		 ./driver/faux11_client.c	\
		 ./platform/linux-spr16-msgs.c	\
		 ./platform/linux-spr16-client.c
		#./driver/faux11_cursor.c
FAUX11_GFX_OBJS := $(FAUX11_GFX_SRCS:.c=.faux11_gfx.o)
# TODO wtf is up with that pixman directory name!?
FAUX11_GFX_INC := -I/usr/include/xorg -I/usr/include/pixman-1

FAUX11_INPUT_SRCS :=	 ./driver/faux11_input.c
FAUX11_INPUT_OBJS := $(FAUX11_INPUT_SRCS:.c=.faux11_input.o)
FAUX11_INPUT_INC := -I/usr/include/xorg -I/usr/include/pixman-1

########################################
# target files
########################################
GTSCREEN := gtscreen
SPR16_EX := spr16_example
FAUX11_GFX   := faux11_drv.so
FAUX11_INPUT := faux11input_drv.so


########################################
# build
########################################
#%.o: %.c
#	$(CC) -c $(DEFLANG) $(CFLAGS) $(DBG) -o $@ $<
%.gtscreen.o: %.c
	$(CC) -c $(DEFLANG) -DSPR16_SERVER $(CFLAGS) $(DBG) -o $@ $<
%.spr16_ex.o: %.c
	$(CC) -c $(DEFLANG) $(CFLAGS) $(DBG) -o $@ $<
%.faux11_gfx.o: %.c
	$(CC) -c -std=gnu99 -pedantic -Wall -fPIC $(DBG) $(FAUX11_GFX_INC) -o $@ $<
%.faux11_input.o: %.c
	$(CC) -c -std=gnu99 -pedantic -Wall -fPIC $(DBG) $(FAUX11_INPUT_INC) -o $@ $<
all:			\
	$(GTSCREEN)	\
	$(SPR16_EX)	\
	$(FAUX11_GFX)	\
	$(FAUX11_INPUT)

########################################
# targets
########################################
$(GTSCREEN):		$(GTSCREEN_OBJS)
			$(CC) $(DEFINES) $(LDFLAGS) $(GTSCREEN_OBJS) -o $@
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

$(FAUX11_GFX):		$(FAUX11_GFX_OBJS)
			$(CC) $(LDFLAGS) -shared $(FAUX11_GFX_OBJS) -o $@
			@echo ""
			@echo "x---------------------x"
			@echo "| faux11_gfx       OK |"
			@echo "x---------------------x"
			@echo ""

$(FAUX11_INPUT):	$(FAUX11_INPUT_OBJS)
			$(CC) $(LDFLAGS) -shared -lX11 $(FAUX11_INPUT_OBJS) -o $@
			@echo ""
			@echo "x---------------------x"
			@echo "| faux11_input     OK |"
			@echo "x---------------------x"
			@echo ""




clean:
	@$(foreach obj, $(GTSCREEN_OBJS), rm -fv $(obj);)
	@$(foreach obj, $(SPR16_EX_OBJS), rm -fv $(obj);)
	@$(foreach obj, $(FAUX11_GFX_OBJS), rm -fv $(obj);)
	@$(foreach obj, $(FAUX11_INPUT_OBJS), rm -fv $(obj);)

	@-rm -fv ./$(GTSCREEN)
	@-rm -fv ./$(SPR16_EX)
	@-rm -fv ./$(FAUX11_GFX)
	@-rm -fv ./$(FAUX11_INPUT)
	@echo "cleaned."

