#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <stdbool.h>
#include <pthread.h>
#include "queue.h"

#define USE_AESD_CHAR_DEVICE 1

#if (USE_AESD_CHAR_DEVICE == 1)
#   define FILENAME "/dev/aesdchar"
#   define REMOVE_FILE(file)
#else
#   define FILENAME "/var/tmp/aesdsocketdata"
#   define REMOVE_FILE(file) (\
        printf("removing file %s\n", file), \
        remove(file))
#endif

#define PORT "9000"
#define BACKLOG 10
#define MAXDATASIZE 25000
#define PRINT_TIMESTAMP_INTERVAL 10

typedef struct Thread_Data{
    int connection_descriptor;
    pthread_t thread;
    pthread_mutex_t *mutex;
    bool active;
    int thread_id;
    SLIST_ENTRY(Thread_Data) entries;
}Thread_Data;

void perror_d(const char* error);

static void signal_handler();

int read_line(char *line,int max_len,off_t offset);