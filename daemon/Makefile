# modbus daemon
ifeq ($(ARCH), armhf)
CROSS_COMPILER:=arm-linux-gnueabihf-
else
CROSS_COMPILER:=x86_64-linux-gnu-
endif
CC:=$(CROSS_COMPILER)gcc

CFLAGS=-g -Wall
LDFLAGS=-lmx-dx -lparson -lpthread -lmariadb -lmx-cmd-agent

all: fbdaemon

fbdaemon:
	mkdir -p ./bin/
	$(CC) -o ./bin/$@ $(CFLAGS) $(LDFLAGS) ./main.c

clean:
	rm fbdaemon

.PHONY: all fbdaemon
