TARGET = modbusfs
SRCS = methods.c

CFLAGS := -Wall -O2 -D_GNU_SOURCE
CFLAGS += $(shell pkg-config --cflags fuse)
CFLAGS += $(shell pkg-config --cflags libmodbus)

LDLIBS := $(shell pkg-config --libs fuse)
LDLIBS += $(shell pkg-config --libs libmodbus)

all: $(TARGET)

.depend depend dep :
	$(CC) $(CFLAGS) -M $(SRCS) > .depend

ifeq ($(wildcard .depend),.depend)
include .depend
endif

$(SRCS:.c=.o) : .depend

$(TARGET): $(TARGET:=.o) $(SRCS:.c=.o)

clean:
	rm -rf $(TARGET) $(TARGET:=.o) $(SRCS:.c=.o) .depend

.PHONY: all clean .depend depend dep
