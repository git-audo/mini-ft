#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include "../errlib.h"
#include "../sockwrap.h"

#define FILELEN 128
#define BUFFSIZE 128
#define TIMEOUT 15

char *prog_name;

int main (int argc, char *argv[]){
    uint16_t port;
    struct in_addr server_addr;
    struct sockaddr_in saddr;
    int socket;
    struct timeval tv;
    fd_set cset;

    size_t n;
    char *pre = "GET ";
    char *post = "\r\n";
    uint32_t filesize;
    uint32_t timestamp;    
    uint32_t rec_bytes = 0;
    int rec_window = BUFFSIZE;
    int f_counter;
    char openfile[FILELEN];
    int namelen;
    
    FILE *fp;

    char buff[BUFFSIZE];

    if(argc < 4){
	printf("Wrong number of arguments, specify server address, port and file/files\n");
	exit(1);
    }

    if((atoi(argv[2]) < 1024) | (atoi(argv[2]) > 65535))
	err_sys("Invalid port - out of range");
    
    prog_name = argv[0];
    port = atoi(argv[2]);
    port = htons(port);

    if((inet_aton(argv[1], &server_addr)) == 0){
	err_sys("Invalid address\n");
    }

    socket = Socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

    bzero(&saddr, sizeof(saddr));
    saddr.sin_family = AF_INET;
    saddr.sin_port = port;
    saddr.sin_addr = server_addr;

    Connect(socket, (struct sockaddr *)&saddr, sizeof(saddr));

    for(f_counter=3; f_counter<argc; f_counter++){
	namelen = strlen(argv[f_counter]);
	
	memset(buff, 0, BUFFSIZE);
	strcat(buff, pre);
	strcat(buff, argv[f_counter]);    
	strcat(buff, post);

	FD_ZERO(&cset);
	FD_SET(socket, &cset);
	tv.tv_sec = TIMEOUT;
	tv.tv_usec = 0;
	
	n = Select(FD_SETSIZE, NULL, &cset, NULL, &tv);
	if(n>0){
	    n = send(socket, buff, namelen+6, MSG_NOSIGNAL);	    
	    if(n==0 | n==-1)
		break;
	}else{
	    printf("\n Timeout expired, no response 1\n");
	    break;
	}

	n = Select(FD_SETSIZE, &cset, NULL, NULL, &tv);
	if(n>0){
	    recv(socket, &buff, 5, 0);
	    if(n==0 | n==-1)
		break;
	}else{
	    printf("\n Timeout expired, no response 1\n");
	    break;
	}

	if(!strncmp(buff, "-ERR\r\n", 5)){
	    printf("File %s not available: closing connection\n", argv[f_counter]);
	    break;
	}

	if(strncmp(buff, "+OK\r\n", 5)){
	    printf("Format error: closing connection\n");
	    break;
	}

	//	sprintf(openfile, "%s_tran", argv[f_counter]);	
	sprintf(openfile, "%s", argv[f_counter]);	
	fp = fopen(openfile, "wb");
	if(fp == NULL){
	    printf(" Error opening file\n");
	    break;
	}
	
	n = Select(FD_SETSIZE, &cset, NULL, NULL, &tv);
	if(n>0){
	    recv(socket, &filesize, 4, 0);	    
	    if(n==0 | n==-1)
		break;
	}else{
	    printf("\n Timeout expired, no response 1\n");
	    break;
	}

	filesize = ntohl(filesize);

	rec_window = BUFFSIZE;
	if(filesize < rec_window)
	    rec_window = filesize;
	rec_bytes = 0;

	n = Select(FD_SETSIZE, &cset, NULL, NULL, &tv);
	if(n>0){
	    while((n = recv(socket, buff, rec_window, 0)) > 0){
		rec_bytes += n;
		if(filesize-rec_bytes < rec_window)
		    rec_window = filesize-rec_bytes;
	
		fwrite(buff, 1, n, fp); 
		n = Select(FD_SETSIZE, &cset, NULL, NULL, &tv);
		if(n <= 0){
		    printf("\n Timeout expired, no response inside while\n");		    
		    break;
		}
	    }
	}else{
	    printf("\n Timeout expired, no response 1\n");
	    break;
	}

	if(rec_bytes < filesize){
	    printf(" The file wasn't transferred correctly\n");
	    if(remove(openfile) != 0)
		printf(" Some error occurred while deleting the corrupted file\n");
	    break;
	}
	fclose(fp);

	n = Select(FD_SETSIZE, &cset, NULL, NULL, &tv); 
	if(n>0){
	    recv(socket, &timestamp, 4, 0);	    
	    if(n==0 | n==-1)
		break;
	}else{
	    printf("\n Timeout expired, no response 1\n");
	    break;
	}

	filesize = ntohl(timestamp);
	printf("Transferred file %s\n File size %lu\n Last modification %lu\n", argv[f_counter], filesize, timestamp);    
    }
    
    Close(socket);
    return 0;
}
