#include "aesdsocket.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <signal.h>
#include <stdbool.h>
#include <syslog.h>
#include <fcntl.h>
#include <pthread.h>
#include <time.h>

#define PORT "9000"
#define BACKLOG 10
#define MAXDATASIZE 20000
#define FILENAME "/var/tmp/aesdsocketdata"
#define PRINT_TIMESTAMP_INTERVAL 10

bool ON=true;
bool deamon=false;
bool verbose=false;
int previous_data_length=0;
int file_descriptor;
int thread_id=0;
time_t ref_time=0;

void perror_d(const char* error){
    if(!deamon && verbose) perror(error);
}

void printf_d(const char* str, ...){
    va_list args;
    if(!deamon && verbose){
        printf_d(str,args);
    }
}

static void signal_handler(){
    ON=false;
}

void* time_update(void *lock){
    pthread_mutex_t* llock=(pthread_mutex_t* ) lock;
    time_t current_time;
    struct tm *current_tm;
    static char time_str[64];
    strcpy(time_str,"timestamp:");

    while(ON){
        time(&current_time);
        
        if (current_time - ref_time >= PRINT_TIMESTAMP_INTERVAL){
            current_tm = localtime(&current_time);
            
            ref_time=current_time;
            int len=strftime(&time_str[10], 54, "%a, %d %b %Y %T %z", current_tm);
            time_str[10+len]='\n';
            time_str[10+len+1]='\0';

            if (pthread_mutex_lock(llock) != 0)
                perror_d("time_update:pthread_mutex_lock");

            if(write(file_descriptor, time_str,10+len+1)<0)
                perror_d("time_update:writing data to file");

            if(pthread_mutex_unlock(llock)!=0)
                perror_d("time_update:pthread_mutex_lock");

            printf_d("%s", time_str);
        }
        usleep(1000);
    }
    return NULL;
}

int read_line(char *line,int max_len,off_t offset){
    off_t llindex=0, loffset=offset;
    char sign;
    int ret_code;

    while(ON){
        ret_code=pread(file_descriptor,&sign,1,loffset);
         switch (ret_code){
            case -1:
                perror_d("read_line: -1");
                return -1;
            case 0:
                return 0;
        }

        line[llindex]=sign;
        llindex++;
        loffset++;

        if(sign == '\n')
            break;

        if(llindex+1 > max_len){
            errno=ENOBUFS;
            perror_d("buffer exeeded");
            return -1;
        }
    }
    line[llindex+1]='\0';
    return llindex;
}

void* read_send_server_loop(void *thread_param2){
    Thread_Data *td = (Thread_Data *) thread_param2;
    int connection_descriptor=td->connection_descriptor;

    int numbytes=0;  
    char *buffer = malloc(sizeof(char)* MAXDATASIZE);

    printf_d("receiving data\n");
    while(ON){
        // reveive data from client 
        numbytes = recv(connection_descriptor, buffer, MAXDATASIZE-1, 0);

        if(numbytes<=0){
            switch (numbytes){
                case -1:
                    perror_d("connection error");
                    break;
                case 0:
                    printf_d("connection closed by remote host\n");
                    break;
            }
            close(connection_descriptor);
            break; 
        }

        buffer[numbytes]='\0';

        if(previous_data_length>numbytes){
            printf_d("numbytes=%d < previous_data_len=%d file position moved to 0\n",numbytes,previous_data_length);
           // ftruncate(file_descriptor,0);
        }

        //write data to file
        if(file_descriptor!=-1){
            printf_d("received and writing to file: %s size=%d\n",buffer,numbytes);

            if (pthread_mutex_lock(td->mutex) != 0)
                perror_d("pthread_mutex_lock");

            if(write(file_descriptor, buffer,numbytes)<0)
                perror_d("writing data to file");
            previous_data_length=numbytes;

            if(pthread_mutex_unlock(td->mutex)!=0)
                perror_d("pthread_mutex_lock");
        }

        if(buffer[numbytes-1]=='\n'){
            printf_d("remote host finished sending a data\n");
            break;
        }
    }

    // read entire content from file line by line and send back to the client
    off_t offset=0;
    int line_length;

    while(ON){
        if (pthread_mutex_lock(td->mutex) != 0)
            perror_d("pthread_mutex_lock");

        line_length=read_line(buffer,MAXDATASIZE,offset);

        if(pthread_mutex_unlock(td->mutex)!=0)
            perror_d("pthread_mutex_lock");

        if(line_length<=0)
            break;

        printf_d("sent back: %s len=%d\n",buffer,line_length);

        offset +=(off_t)line_length;

        if(send(connection_descriptor, buffer, line_length, 0) == -1)
            perror_d("send");
    }

    free(buffer);
    td->active=false;
    return td;
}

int main(int argc, char* argv[]){
    signal(SIGINT,signal_handler);
    signal(SIGTERM,signal_handler);
    openlog(NULL,0,LOG_USER);

    if (argc > 0){
        for(int i=1; i<argc; i++){
            if(strcmp(argv[i],"-d")==0) deamon=true;
            if(strcmp(argv[i],"-v")==0) verbose=true;  
        }
    }

    int socket_descriptor, getaddrinfo_response, bind_response;
    struct addrinfo hints, *servinfo;

    memset(&hints,0,sizeof hints);
    hints.ai_family=AF_INET;
    hints.ai_socktype=SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;
    
    getaddrinfo_response=getaddrinfo(NULL, PORT, &hints, &servinfo);
    if(getaddrinfo_response!=0){
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(getaddrinfo_response));
        return 1;
    }

    socket_descriptor = socket(servinfo->ai_family, servinfo->ai_socktype | SOCK_NONBLOCK,servinfo->ai_protocol);
    if(socket_descriptor==-1){
        perror("server: socket");
        return 1;
    }
    if (setsockopt(socket_descriptor, SOL_SOCKET, SO_REUSEADDR, &(int){1}, sizeof(int)) < 0)
        perror("setsockopt");

    bind_response=bind(socket_descriptor, servinfo->ai_addr, servinfo->ai_addrlen);
    freeaddrinfo(servinfo);
    if(bind_response==-1){
        perror("server: bind");
        close(socket_descriptor);
        return 1;
    }

    if(deamon){
        int process_id = fork (); //create child proccess
        if(process_id == -1){
            perror("run as a deamon failed:");
            close(socket_descriptor);
            return -1;
        }else if(process_id >0){
            close(socket_descriptor);
            return 0;
        }
    }

    if(listen(socket_descriptor, BACKLOG)==-1){
        perror_d("listen");
        return 1;
    }

    file_descriptor = open(FILENAME, O_RDWR | O_TRUNC | O_CREAT | O_APPEND, 0644);
    if(file_descriptor==-1){
        perror_d("file open");
        return 1;
    }

    pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
    pthread_mutex_init(&mutex, NULL);

    pthread_t time_thread;
    pthread_create(&time_thread, NULL, time_update,&mutex);

    SLIST_HEAD(slisthead, Thread_Data) head;
    SLIST_INIT(&head);
    Thread_Data* td=NULL;
    Thread_Data* td_tmp=NULL;

    printf_d("DEBUG 1\n");

    while(ON){
        td=(Thread_Data *) malloc(sizeof(Thread_Data));
        td->active=true;
        td->mutex=&mutex;
        td->thread_id=thread_id;
        thread_id++;

        SLIST_INSERT_HEAD(&head, td, entries);

        struct sockaddr_in remote_addr;
        socklen_t remote_addr_size = sizeof (struct sockaddr_in);

        printf_d("wait for socket accept\n");

        while(ON){
            td->connection_descriptor = accept(socket_descriptor,(struct sockaddr *)&(remote_addr),&remote_addr_size);
            if(td->connection_descriptor != -1) break;
            usleep(1000);
        }
    
        if(!ON){
            if(close(td->connection_descriptor)==-1)
                perror_d("close connection_descriptor");
            break;
        }

        printf_d("Accepted connection from %s:%d\n",inet_ntoa(remote_addr.sin_addr),ntohs(remote_addr.sin_port));
        syslog(LOG_INFO,"Accepted connection from %s\n",inet_ntoa(remote_addr.sin_addr));

        printf_d("--->Starting new thread ID %d, connection desc %d pointer %p\n",td->thread_id,td->connection_descriptor,td);

        pthread_create(&(td->thread), NULL, read_send_server_loop,td);

        SLIST_FOREACH_SAFE(td, &head, entries,td_tmp) {
            if (!(td->active)){

                printf_d("--->Closing thread ID %d, connection desc %d pointer %p\n",td->thread_id,td->connection_descriptor,td);
                SLIST_REMOVE(&head,td,Thread_Data,entries);

                if(close(td->connection_descriptor)==-1)
                    perror_d("close connection_descriptor");
                syslog(LOG_INFO,"Closed connection from %s\n",inet_ntoa(remote_addr.sin_addr));

                if(pthread_join((td->thread),NULL)==-1){
                    perror_d("pthread_join");
                }
                free(td);
            }
        }
    }

    pthread_join(time_thread,NULL);

    close(file_descriptor);
    remove(FILENAME);
    if(close(socket_descriptor)==-1)
        perror_d("close socket_descriptor");

    printf_d("closed socket_descriptor\n");

    pthread_mutex_destroy(&mutex);

    return 0;
}