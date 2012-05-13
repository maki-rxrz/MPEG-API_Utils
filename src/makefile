TARGET  = ../bin/cutcaption.exe
SRCS    = cutcaption.c avs_utils.c mpegts_utils.c mpeg_stream.c
OBJS    = $(SRCS:%.c=%.o)

CC      = gcc
LD      = gcc
STRIP   = strip

CFLAGS  = -Wall -std=gnu99
LDFLAGS = 

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJS)
	$(LD) $(LDFLAGS) -o $@ $^
	$(STRIP) $@

%.o: %.c .depend
	$(CC) -c $(CFLAGS) $< -o $@

clean:
	$(RM) *.o config.* .depend

ifneq ($(wildcard .depend),)
include .depend
endif

.depend:
	@$(RM) .depend
	@$(foreach SRC, $(SRCS), $(CC) $(SRC) $(CFLAGS) -MT $(SRC:%.c=%.o) -MM >> .depend;)
