CC=$(CROSS_COMPILE)gcc
CFLAGS += -Wall -Werror

aesdsocket: aesdsocket.c
	$(CC) $(CFLAGS) aesdsocket.c -o aesdsocket

.PHONY:all
all: aesdsocket

.PHONY:default
default: aesdsocket


.PHONY:clean
clean:
	rm -f aesdsocket *.o