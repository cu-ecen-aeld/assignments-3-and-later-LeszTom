#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <stdbool.h>
#include <pthread.h>
#include "queue.h"

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