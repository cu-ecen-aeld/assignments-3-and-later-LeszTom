#include <stdlib.h>
#include <stdio.h>
#include <syslog.h>
#include <errno.h>
#include <string.h>

int main(int argc, char* argv[])
{
    openlog(NULL, 0, LOG_USER);
    if (argc < 3){
        printf("file or text is missing\n");
        syslog(LOG_ERR, "file or text is missing");
        exit(1);
    }
    
    syslog(LOG_DEBUG, "Writing %s to %s",argv[2],argv[1]);
    FILE *file;
    file = fopen(argv[1],"w");
    char text[1024];
    strcpy(text, argv[2]);
    fprintf(file,"%s",text);
    fclose(file);
    closelog();
    if (errno != 0){
        syslog(LOG_ERR, "Unexpected error occured, errno: %d",errno);
    }
    exit(0);
}