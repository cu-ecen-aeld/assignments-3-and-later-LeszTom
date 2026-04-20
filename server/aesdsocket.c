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
#include <stdarg.h>
#include "aesdsocket.h"
#include "../aesd-char-driver/aesd_ioctl.h"

bool ON=true;
bool deamon=false;
bool verbose=false;
int file_descriptor;
unsigned long thread_id=0;
time_t ref_time=0;

void perror_d(const char* error){
    if(!deamon && verbose) perror(error);
}

void printf_d(const char* str, ...){
    if(!deamon && verbose){
        va_list args;
        va_start(args, str);
        vprintf(str,args);
        va_end(args);
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

void* read_send_server_loop(void *thread_param2){
    Thread_Data *td = (Thread_Data *) thread_param2;
    int connection_descriptor=td->connection_descriptor;

    ssize_t numbytes=0;
    off_t offset=0;
    char *buffer = malloc(sizeof(char)* MAXDATASIZE);

    int file_des = open(FILENAME, O_RDWR | O_CREAT | O_APPEND, 0644);

    printf_d("receiving data\n");
    while(ON)
    {
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
            break; 
        }

        buffer[numbytes]='\0';

        if(strstr(buffer,"AESDCHAR_IOCSEEKTO:")){
            struct aesd_seekto seekto = {
                atoi(strtok(buffer+sizeof("AESDCHAR_IOCSEEKTO:")-1,",")),
                atoi(strtok(NULL," "))};

            //write data to file
            if(file_des!=-1){
                if (pthread_mutex_lock(td->mutex) != 0)
                    perror_d("pthread_mutex_lock");

                offset = ioctl(file_des,AESDCHAR_IOCSEEKTO,&seekto);
                if(offset<0)
                    perror_d("Error aesdsocket ioctl:");

                if(pthread_mutex_unlock(td->mutex)!=0)
                    perror_d("pthread_mutex_lock");
            }
            break;
        }

        //write data to file
        if(file_des!=-1){
            printf_d("received and writing to file: %s|size=%zd\n",buffer,numbytes);

            if (pthread_mutex_lock(td->mutex) != 0)
                perror_d("pthread_mutex_lock");

            if(write(file_des, buffer,numbytes)<0)
                perror_d("writing data to file");

            if(pthread_mutex_unlock(td->mutex)!=0)
                perror_d("pthread_mutex_lock");
        }
    
        if(buffer[numbytes-1]=='\n'){
            printf_d("remote host finished sending a data\n");
            break;
        }
    }

    if(offset >= 0){
        // read entire content from file line by line and send back to the client
        ssize_t buf_len=0;
        memset(buffer,0,MAXDATASIZE);
        lseek(file_des, offset, SEEK_SET);

        while(ON){
            if (pthread_mutex_lock(td->mutex) != 0)
                perror_d("pthread_mutex_lock");

            buf_len=read(file_des,buffer,MAXDATASIZE);

            if(pthread_mutex_unlock(td->mutex)!=0)
                perror_d("pthread_mutex_lock");

            printf_d("data to sent back: %s|buf_len=%d\n",buffer,buf_len);

            if(buf_len > 0 ){
                printf_d("sending: %s|len=%d\n",buffer,buf_len);
                if(send(connection_descriptor, buffer, buf_len, 0) == -1)
                    perror_d("send");
            }else{
                printf_d("Read len %zd. No data sent\n",buf_len);
                break;
            }
        }
    }

    close(file_des);
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

    printf_d("Using device: %s\nUSE_AESD_CHAR_DEVICE: %d\n",FILENAME,USE_AESD_CHAR_DEVICE);

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

    pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
    pthread_mutex_init(&mutex, NULL);

    SLIST_HEAD(slisthead, Thread_Data) head;
    SLIST_INIT(&head);
    Thread_Data* td=NULL;
    Thread_Data* td_tmp=NULL;

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
    
        if(!ON)
            break;

        printf_d("Accepted connection from %s:%d\n",inet_ntoa(remote_addr.sin_addr),ntohs(remote_addr.sin_port));
        syslog(LOG_INFO,"Accepted connection from %s\n",inet_ntoa(remote_addr.sin_addr));

        printf_d("--->Starting new thread ID %lu, connection desc %d pointer %p\n",td->thread_id,td->connection_descriptor,(void *)td);

        pthread_create(&(td->thread), NULL, read_send_server_loop,td);

        SLIST_FOREACH_SAFE(td, &head, entries,td_tmp) {
            if (!(td->active)){

                printf_d("--->Closing thread ID %d, connection desc %d pointer %p\n",td->thread_id,td->connection_descriptor,td);
                SLIST_REMOVE(&head,td,Thread_Data,entries);

                if(close(td->connection_descriptor)==-1)
                    perror_d("close connection_descriptor 2");
                syslog(LOG_INFO,"Closed connection from %s\n",inet_ntoa(remote_addr.sin_addr));

                if(pthread_join((td->thread),NULL)==-1){
                    perror_d("pthread_join");
                }
                free(td);
            }
        }
    }

    REMOVE_FILE(FILENAME);
    if(close(socket_descriptor)==-1)
        perror_d("close socket_descriptor");

    printf_d("closed socket_descriptor\n");

    pthread_mutex_destroy(&mutex);
    return 0;
}