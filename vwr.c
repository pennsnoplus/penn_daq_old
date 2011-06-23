/*
 ** tut.c -- a simple viewer
 ** client which echoes any text received
 ** to the screen.
 **
 ** Written by Peter Downs (August 4th, 2010)
 **	
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <netdb.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <signal.h>

#define MAXDATASIZE 100 // max number of bytes we can get at once 

int sockfd, numbytes;
pid_t pid;

// get sockaddr, IPv4 or IPv6:
void *get_in_addr(struct sockaddr *sa)
{
	if (sa->sa_family == AF_INET) {
		return &(((struct sockaddr_in*)sa)->sin_addr);
	}
	
	return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

void leave(int sig){
	fprintf(stderr, "VWR: now exiting\n");
	close(sockfd);
	exit(0);
}

int main(int argc, char *argv[])
{
	(void) signal(SIGINT,leave);
	
	struct addrinfo hints, *servinfo, *p;
	int rv;
	char s[INET6_ADDRSTRLEN];
	
	if (argc != 3) {
	    fprintf(stderr,"takes two arguments: hostname port\n");
	    leave(SIGINT);
	}
	
	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_STREAM;
	
	if ((rv = getaddrinfo(argv[1], argv[2], &hints, &servinfo)) != 0) {
		fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
		leave(SIGINT);
	}
	
	// loop through all the results and connect to the first we can
	for(p = servinfo; p != NULL; p = p->ai_next) {
		if ((sockfd = socket(p->ai_family, p->ai_socktype,
							 p->ai_protocol)) == -1) {
			perror("VWR: socket");
			continue;
		}
		
		if (connect(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
			close(sockfd);
			perror("VWR: connect");
			continue;
		}
		
		break;
	}
	
	if (p == NULL) {
		fprintf(stderr, "VWR: failed to connect\n");
		leave(SIGINT);
	}
	
	inet_ntop(p->ai_family, get_in_addr((struct sockaddr *)p->ai_addr),
			  s, sizeof s);
	printf("VWR: a simple VieWeR program\n");
	printf("CONNECTED TO  %s:%s\n", s, argv[2]);
	printf("******************************\n");

	freeaddrinfo(servinfo); // all done with this structure

	char response[MAXDATASIZE];
	while(1){
		memset(response, '\0', MAXDATASIZE);	// clear the response buffer
		numbytes = recv(sockfd, response, MAXDATASIZE-1, 0);
		if (numbytes > 0){
			if(strncmp(response, "new_daq: cleared screen", 23) == 0){
				system("clear");
			}
			write(1, response, MAXDATASIZE);
			//write(1, "\n", 1);
		}
		else if (numbytes == 0){
			printf("VWR: connection closed by server\n");
			break;
		}
	}
	leave(SIGINT);
	return 0;
}
