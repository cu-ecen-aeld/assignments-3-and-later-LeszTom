#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <signal.h>
#include <stdbool.h>
#include <syslog.h>
#include <fcntl.h>
#include <signal.h>

#define PORT "9000"
#define BACKLOG 10
#define MAXDATASIZE 20000
#define FILENAME "/var/tmp/aesdsocketdata"

bool ON=true;
bool deamon=false;

void perror_d(const char* error){
    if(!deamon) perror(error);
}

static void signal_handler(){
    ON=false;
    printf("\nsignal_handler called\n");
}

int read_line(int file_descriptoresc,char *line,int max_len,off_t offset){
    off_t llindex=0, loffset=offset;
    char sign;
    int ret_code;

    while(ON){
        ret_code=pread(file_descriptoresc,&sign,1,loffset);
         switch (ret_code){
            case -1:
                perror_d("read_line: -1");
                return -1;
            case 0:
                perror_d("read_line: EOF");
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

int main(int argc, char* argv[]){
    signal(SIGINT,signal_handler);
    signal(SIGTERM,signal_handler);

    if (argc > 0){
        for(int i=1; i<argc; i++){
            if(strcmp(argv[i],"-d")==0) deamon=true;    
        }
    }

    int socket_descriptor, getaddrinfo_response, bind_response;
    struct addrinfo hints, *servinfo;

    memset(&hints,0,sizeof hints);
    hints.ai_family=AF_INET;
    hints.ai_socktype=SOCK_STREAM;

    openlog(NULL,0,LOG_USER);

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

    int process_id;
    if(deamon){
        printf("running as a deamon\n");
        process_id = fork (); //create child proccess
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

    int connection_descriptor;
    struct sockaddr_in remote_addr;
    socklen_t remote_addr_size = sizeof remote_addr;

    int file_descriptor;
    file_descriptor = open(FILENAME, O_RDWR | O_TRUNC | O_CREAT | O_APPEND, 0644);
    if(file_descriptor==-1)
        perror_d("file open");

    int max_data_received=0;

    while(ON){
        if(!deamon) printf("wait for socket accept\n");
        while((connection_descriptor = accept(socket_descriptor,(struct sockaddr *)&remote_addr,&remote_addr_size))==-1 && ON)
            usleep(1000);
        
        if(!ON){
            if(close(connection_descriptor)==-1)
                perror_d("close connection_descriptor");
            break;
        } 
        
        if(!deamon) printf("Accepted connection from %s:%d\n",inet_ntoa(remote_addr.sin_addr),ntohs(remote_addr.sin_port));
        syslog(LOG_INFO,"Accepted connection from %s\n",inet_ntoa(remote_addr.sin_addr));

        int numbytes;  
        char *buffer = malloc(sizeof(char)* MAXDATASIZE);

        if(!deamon) printf("receiving data\n");
        while(ON){
            // reveive data from client 
            numbytes = recv(connection_descriptor, buffer, MAXDATASIZE-1, 0);

            if(numbytes<=0){
                switch (numbytes){
                    case -1:
                        perror_d("connection error");
                        break;
                    case 0:
                        if(!deamon) printf("connection closed by remote host\n");
                        break;
                }
                close(connection_descriptor);
                break; 
            }

            buffer[numbytes]='\0';

            if(max_data_received>numbytes){
                if(!deamon) printf("numbytes=%d < max_data_received=%d file position moved to 0\n",numbytes,max_data_received);
                ftruncate(file_descriptor,0);
            }
            max_data_received=numbytes;

            //write data to file
            if(file_descriptor!=-1){
                if(!deamon) printf("received and writing to file: %s size=%d\n",buffer,numbytes);
                if(write(file_descriptor, buffer,numbytes)<0)
                    perror_d("writing data to file");
            }

            if(buffer[numbytes-1]=='\n'){
                if(!deamon) printf("remote host finished sending a data\n");
                break;
            }
        }

        // read entire content from file line by line and send back to the client
        off_t offset=0;
        int line_length;
        while((line_length=read_line(file_descriptor,buffer,MAXDATASIZE,offset))>0){
            if(!deamon) printf("sent back: %s len=%d\n",buffer,line_length);
            offset +=(off_t)line_length;
            if(send(connection_descriptor, buffer, line_length, 0) == -1)
                perror_d("send");
        }

        free(buffer);
        if(close(connection_descriptor)==-1)
            perror_d("close connection_descriptor");

        syslog(LOG_INFO,"Closed connection from %s\n",inet_ntoa(remote_addr.sin_addr));
    }

    close(file_descriptor);
    remove(FILENAME);
    if(close(socket_descriptor)==-1)
        perror_d("close socket_descriptor");
    
    if(!deamon) printf("closed socket_descriptor\n");
    return 0;
}