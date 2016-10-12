#########################################
# global defines
#########################################
# if you have a 64bit system set bitcount(TODO untested).
DEFINES := -DMAX_SYSTEMPATH=1024 -DPTRBITCOUNT=32
CFLAGS  := -pedantic -Wall -Wextra -Werror $(DEFINES)
DEFLANG := -ansi
#DBG	:= -g

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
		 ./platform/linux-spr16-msgs.c	\
		 ./platform/linux-spr16-server.c

GTSCREEN_OBJS := $(GTSCREEN_SRCS:.c=.gtscreen.o)

SPR16_EX_SRCS := ./examples/spr16-example.c		\
		 ./examples/game.c			\
		 ./examples/dynamics.c			\
		 ./examples/moon.c			\
		 ./examples/craft.c			\
		 ./platform/linux-spr16-msgs.c		\
		 ./platform/linux-spr16-client.c
SPR16_EX_OBJS := $(SPR16_EX_SRCS:.c=.spr16_ex.o)

########################################
# target files
########################################
GTSCREEN := gtscreen
SPR16_EX := spr16_example


########################################
# build
########################################
#%.o: %.c
#	$(CC) -c $(DEFLANG) $(CFLAGS) $(DBG) -o $@ $<
%.gtscreen.o: %.c
	$(CC) -c $(DEFLANG) -DSPR16_SERVER $(CFLAGS) $(DBG) -o $@ $<
%.spr16_ex.o: %.c
	$(CC) -c $(DEFLANG) $(CFLAGS) $(DBG) -o $@ $<

all:			\
	$(GTSCREEN)	\
	$(SPR16_EX)

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


clean:
	@$(foreach obj, $(GTSCREEN_OBJS), rm -fv $(obj);)
	@$(foreach obj, $(SPR16_EX_OBJS), rm -fv $(obj);)

	@-rm -fv ./$(GTSCREEN)
	@-rm -fv ./$(SPR16_EX)
	@echo "cleaned."

