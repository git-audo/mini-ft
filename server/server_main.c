#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <sys/stat.h>
#include <signal.h>
#include <sys/wait.h>
#include <pthread.h>
#include "../libs/errlib.h"
#include "../libs/sockwrap.h"

#define TIMEOUT 15
#define BUFFSIZE 128
#define NAMELEN 128

char *prog_name;

pthread_mutex_t file_lock = PTHREAD_MUTEX_INITIALIZER;

void transferFiles(int socket);
void childHandler(int signal);
void log_event(char *message);

int main (int argc, char *argv[])
{
    uint16_t port;
    int socket, pass_socket;
    int backlog = 4;
    socklen_t alen;
    struct sockaddr_in s_addr, c_addr;
    int child_pid;

    if(argc != 2){
	printf("Wrong number of arguments, specify port number\n");
	exit(1);
    }

    if((atoi(argv[1]) < 1024) | (atoi(argv[1]) > 65535))
	err_sys("Invalid port - out of range");

    prog_name = argv[0];
    port = atoi(argv[1]);

    if(sscanf(argv[1], "%" SCNu16, &port) != 1)
	err_sys("Invalid port");

    signal(SIGCHLD, childHandler);

    port = htons(port);

    socket = Socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

    bzero(&s_addr, sizeof(s_addr));
    s_addr.sin_family = AF_INET;
    s_addr.sin_port = port;
    s_addr.sin_addr.s_addr = INADDR_ANY;
    Bind(socket, (struct sockaddr *)&s_addr, sizeof(s_addr));

    Listen(socket, backlog);

    pass_socket = socket;

    printf("File transfer server started - listening for connections ..\n");

    for(;;){
	alen = sizeof(struct sockaddr_in);
	if((socket = accept(pass_socket, (struct sockaddr *)&c_addr, &alen)) < 0){
	    printf(" Failed to accept connection request\n");
	    continue;
	}

	showAddr("\nClient connected", &c_addr);

	if((child_pid = fork()) < 0){
	    err_msg("failed to execute fork");
	    close(socket);
	} else if(child_pid == 0){
	    close(pass_socket);
	    transferFiles(socket);
	    exit(1);
	} else {
	    close(socket);
	    printf("Process %d serving client\nListening for new connections ..\n", child_pid);
	}
    }
}

void childHandler(int signal){
    pid_t child;
    child = wait(NULL);
    
    char message[NAMELEN];
    memset(message, 0, NAMELEN);	        
    snprintf(message, NAMELEN, "Proc %d: client connection ended\n", child);
    log_event(message);    	    
}

void transferFiles(int socket){
    char buff[BUFFSIZE] = "\0";
    char filename[NAMELEN] = "\0";
    int n, nread;
    FILE *fp;

    uint32_t filesize = 0;
    uint32_t modtime = 0;

    fd_set cset;
    struct timeval tv;

    char error_mess[7] = "\0";
    snprintf(error_mess, 7, "-ERR\r\n");

    char message[NAMELEN];
    memset(message, 0, NAMELEN);	    
    
    for(;;){

	FD_ZERO(&cset);
	FD_SET(socket, &cset);
	tv.tv_sec = TIMEOUT;
	tv.tv_usec = 0;	
	n = select(FD_SETSIZE, &cset, NULL, NULL, &tv);
	if(n>0){
	    n = recv(socket, buff, BUFFSIZE, 0);
	    if(n==0 | n==-1)
		break;
	}else{
	    printf("\n Timeout expired, no response 1\n");
	    break;
	}

	if(strncmp(buff, "GET ", 4) != 0){
	    printf(" Wrong format - closing connection\n");
	    n = send(socket, error_mess, 6, MSG_NOSIGNAL);	    
	    break;
	}

	memset(filename, 0, NAMELEN);
	strncpy(filename, buff+4, n-6);
	//	printf("  %s", filename);
	//	fflush(stdout);

	if(strstr(filename, "../") != NULL){
	    printf(" %s - file not in working directory\n", filename);
	    n = send(socket, error_mess, 6, MSG_NOSIGNAL);
	    break;
	}
	
	// get file size and last modification time
	struct stat stat_buf;
	stat(filename, &stat_buf);
	if(!S_ISREG(stat_buf.st_mode)){
	    snprintf(message, NAMELEN, "Proc %d: %s - file not available\n", getpid(), filename);
	    log_event(message);
	    
	    n = select(FD_SETSIZE, NULL, &cset, NULL, &tv);
	    if(n>0){
		n = send(socket, error_mess, 6, MSG_NOSIGNAL);
		break;
	    }else{
		printf("\n Timeout expired, no response 2\n");
		break;
	    }
	}

	fp = fopen(filename, "rb");
	if(fp == NULL){
	    snprintf(message, NAMELEN, "Proc %d: %s - file not available\n", getpid(), filename);
	    log_event(message);
	    
	    n = select(FD_SETSIZE, NULL, &cset, NULL, &tv);
	    if(n>0){
		n = send(socket, error_mess, 6, MSG_NOSIGNAL);
		if(n==0 | n==-1)
		    break;
	    }else{
		printf("\n Timeout expired, no response 2\n");
		break;
	    }
	    break;
	}

	filesize = stat_buf.st_size;
	modtime = stat_buf.st_mtime;
	filesize = htonl(filesize);
	modtime = htonl(modtime);    

	char pre[6] = "\0";
	snprintf(pre, 6, "+OK\r\n");

	FD_SET(socket, &cset);
	n = select(FD_SETSIZE, NULL, &cset, NULL, &tv);
	if(n>0){
	    n = send(socket, pre, 5, MSG_NOSIGNAL);
	    if(n==0 | n==-1)
		break;
	}else{
	    printf("\n Timeout expired, no response 3\n");
	    break;
	}

	FD_SET(socket, &cset);	
	n = select(FD_SETSIZE, NULL, &cset, NULL, &tv);
	if(n>0){
	    n = send(socket, &filesize, sizeof(filesize), MSG_NOSIGNAL);
	    if(n==0 | n==-1)
		break;
	}else{
	    printf("\n Timeout expired, no response 4\n");
	    break;
	}

	while((nread = fread(buff, 1, sizeof(buff), fp)) > 0){
	    FD_SET(socket, &cset);	    
	    n = select(FD_SETSIZE, NULL, &cset, NULL, &tv);
	    if(n>0){
	    	n = send(socket, buff, nread, MSG_NOSIGNAL);
	    	if(n==0 | n==-1)
	    	    break;
	    }else{
	    	printf("\n Timeout expired, no response\n");
	    	break;
	    }
	}

	snprintf(message, NAMELEN, "Proc %d: %s - file transfered\n", getpid(), filename);
	log_event(message);
	
	FD_SET(socket, &cset);
	n = select(FD_SETSIZE, NULL, &cset, NULL, &tv);
	if(n>0){
	    n = send(socket, &modtime, sizeof(modtime), MSG_NOSIGNAL);    
	    if(n==0 | n==-1)
		break;
	}else{
	    printf("\n Timeout expired, no response 5\n");
	    break;
	}

	fclose(fp);
    }

    close(socket);
}

void log_event(char *message) {
    FILE *logs;
    pthread_mutex_lock(&file_lock);

    logs = fopen("logs.txt", "a");
    fprintf(logs, message);
    fclose(logs);
    
    pthread_mutex_unlock(&file_lock);
}
