BIN_DIR  = ../bin
API_SRCS = mpeg_utils.c mpeges_parser.c mpegts_parser.c mpeg_stream.c mpeg_video.c

TARGET  = $(BIN_DIR)/cutcaption.exe
SRCS    = cutcaption.c d2v_parser.c avs_utils.c $(API_SRCS)
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
