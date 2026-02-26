CC ?= $(CROSS_COMPILE)gcc
CFLAGS ?= -Wall -Werror
TARGET ?= aesdsocket
LDFLAGS ?= -lpthread -lrt

aesdsocket: aesdsocket.c
	$(CC) $(CFLAGS) aesdsocket.c -o $(TARGET) $(LDFLAGS) 

.PHONY:all
all: aesdsocket

.PHONY:default
default: aesdsocket


.PHONY:clean
clean:
	rm -f aesdsocket *.o
