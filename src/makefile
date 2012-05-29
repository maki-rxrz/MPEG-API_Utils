BIN_DIR  = ../bin
API_SRCS = mpeg_utils.c mpeges_parser.c mpegts_parser.c mpeg_stream.c mpeg_video.c
SRCS     = ts_parser.c cutcaption.c d2v_parser.c avs_utils.c $(API_SRCS)

TARGET1 = $(BIN_DIR)/cutcaption.exe
SRCS1   = cutcaption.c d2v_parser.c avs_utils.c $(API_SRCS)
OBJS1   = $(SRCS1:%.c=%.o)

TARGET2 = $(BIN_DIR)/ts_parser.exe
SRCS2   = ts_parser.c $(API_SRCS)
OBJS2   = $(SRCS2:%.c=%.o)

CC      = $(CROSS)gcc
LD      = $(CROSS)gcc
STRIP   = $(CROSS)strip

CFLAGS  = -Wall -std=gnu99
LDFLAGS = 

.PHONY: all clean

all: $(TARGET1) $(TARGET2)

$(TARGET1): $(OBJS1)
	$(LD) $(LDFLAGS) -o $@ $^
	$(STRIP) $@

$(TARGET2): $(OBJS2)
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
