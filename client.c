/**
 * client.c - tftp client
 */
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <netdb.h>
#include <sys/time.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include "utility.h"

void *get_in_addr(struct sockaddr *sa)
{
	if (sa->sa_family == AF_INET) {
		return &(((struct sockaddr_in*)sa)->sin_addr);
	}
	return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

//CHECKS FOR TIMEOUT
int check_timeout(int sockfd, char *buf, struct sockaddr_storage their_addr, socklen_t addr_len){
	fd_set fds;
	int n;
	struct timeval tv;

	// set up the file descriptor set
	FD_ZERO(&fds);
	FD_SET(sockfd, &fds);

	// set up the struct timeval for the timeout
	tv.tv_sec = TIME_OUT;
	tv.tv_usec = 0;

	// wait until timeout or data received
	n = select(sockfd+1, &fds, NULL, NULL, &tv);
	if (n == 0){
		printf("timeout\n");
		return -2; // timeout!
	} else if (n == -1){
		printf("error\n");
		return -1; // error	
	}

	return recvfrom(sockfd, buf, MAXBUFLEN-1 , 0, (struct sockaddr *)&their_addr, &addr_len);
}

int main(int argc, char* argv[]){
	int sockfd;
	struct addrinfo hints, *servinfo, *p;
	int rv;
	int numbytes;
	char buf[MAXBUFLEN];
	char s[INET6_ADDRSTRLEN];
	struct sockaddr_storage their_addr;
	socklen_t addr_len;

	char hostname[100], file_name[100];
	int mychoice = 0;
	printf("\nEnter hostname: ");
	scanf("%s", hostname);
	char *server = (char *)malloc(strlen(hostname)+1);;
	strcpy(server, hostname);

	//===========CONFIGURATION OF CLIENT - STARTS===========
	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_UNSPEC;
	hints.ai_socktype = SOCK_DGRAM;
	if((rv = getaddrinfo(server, SERVERPORT, &hints, &servinfo)) != 0){
		fprintf(stderr, "CLIENT: getaddrinfo: %s\n", gai_strerror(rv));
		return 1;
	}
	
	// loop through all the results and make a socket
	for(p = servinfo; p != NULL; p = p->ai_next) {
		if ((sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1){
			perror("CLIENT: socket");
			continue;
		}
		break;
	}
	if(p == NULL){
		fprintf(stderr, "CLIENT: failed to bind socket\n");
		return 2;
	} else {
		fprintf(stdout, "\nCLIENT: socket created successfully.\n");
		fprintf(stdout, "CLIENT: %s connected to server\n", hostname);
	}
	//===========CONFIGURATION OF CLIENT - ENDS===========

	printf("\n===== Welcome to TFTP Client =====\n");
	printf("1. Download file\n2. Upload file\n3. Exit client");
	printf("\nSelect one [1/2/3]: ");
	scanf("%d", &mychoice);

	//===========MAIN IMPLEMENTATION - STARTS===========
	if(mychoice == 1){ //GET DATA FROM SERVER
		
		printf("\n-[Download]-");
		printf("\nEnter file name: ");
		scanf("%s", file_name);
		char *file = (char *)malloc(strlen(file_name)+1);
		strcpy(file, file_name);

		//SENDING RRQ
		char *message = make_rrq(file);
		char last_recv_message[MAXBUFLEN];strcpy(last_recv_message, "");
		char last_sent_ack[10];strcpy(last_sent_ack, message);
		if((numbytes = sendto(sockfd, message, strlen(message), 0, p->ai_addr, p->ai_addrlen)) == -1){
			perror("CLIENT: sendto");
			exit(1);
		}
		printf("\nCLIENT: sent %d bytes to %s\n", numbytes, server);

		char filename[MAX_FILENAME_LEN];
		strcpy(filename, file);
		strcat(filename, "_client");

		FILE *fp = fopen(filename, "wb");
		if(fp == NULL){ //ERROR CHECKING
			fprintf(stderr,"CLIENT: error opening file: %s\n", filename);
			exit(1);
		}
		
		//RECEIVING ACTUAL FILE
		int c_written;
		do {
			//RECEIVING FILE - PACKET DATA
			addr_len = sizeof their_addr;
			if ((numbytes = recvfrom(sockfd, buf, MAXBUFLEN-1 , 0, (struct sockaddr *)&their_addr, &addr_len)) == -1) {
				perror("CLIENT: recvfrom");
				exit(1);
			}
			printf("CLIENT: got packet from %s\n", inet_ntop(their_addr.ss_family, get_in_addr((struct sockaddr *)&their_addr), s, sizeof s));
			printf("CLIENT: packet is %d bytes long\n", numbytes);
			buf[numbytes] = '\0';

			//CHECKING IF ERROR PACKET
			if(buf[0]=='0' && buf[1]=='5'){
				fprintf(stderr, "CLIENT: got error packet: %s\n", buf);
				exit(1);
			}

			printf("CLIENT: packet contains \n\n----- Data sent by SERVER -----\n\n%s\n-------------------------------\n", buf);
				
			//SENDING LAST ACK AGAIN - AS IT WAS NOT REACHED
			if(strcmp(buf, last_recv_message) == 0){
				sendto(sockfd, last_sent_ack, strlen(last_sent_ack), 0, (struct sockaddr *)&their_addr, addr_len);
				continue;
			}

			//WRITING FILE - PACKET DATA
			c_written = strlen(buf+4);
			fwrite(buf+4, sizeof(char), c_written, fp);
			strcpy(last_recv_message, buf);

			//SENDING ACKNOWLEDGEMENT - PACKET DATA
			char block[3];
			strncpy(block, buf+2, 2);
			block[2] = '\0';
			char *t_msg = make_ack(block);
			if((numbytes = sendto(sockfd, t_msg, strlen(t_msg), 0, p->ai_addr, p->ai_addrlen)) == -1){
				perror("CLIENT ACK: sendto");
				exit(1);
			}

			printf("CLIENT: sent %d bytes\n", numbytes);
			strcpy(last_sent_ack, t_msg);

		} while(c_written == MAX_READ_LEN);

		printf("NEW FILE: %s SUCCESSFULLY MADE\n", filename);
		fclose(fp);
	} 
	
	else if(mychoice == 2){	//WRITE DATA TO SERVER	

		//MENU
		int choice = 0;
		printf("\n-[Upload]-");
		printf("\n1. Create new file\n");
		printf("2. Upload existing file\n");
		printf("Select [1/2]: ");
		scanf("%d", &choice);

		//CREATE NEW FILE===========================================================================================================
		if(choice == 1){
			
			printf("\nEnter file name: ");
			scanf("%s", file_name);
			char *file = (char *)malloc(strlen(file_name)+1);
			strcpy(file, file_name);

			//SENDING WRQ
			char *message = make_wrq(file);
			char *last_message;
			if((numbytes = sendto(sockfd, message, strlen(message), 0, p->ai_addr, p->ai_addrlen)) == -1){
				perror("CLIENT: sendto");
				exit(1);
			}
			printf("CLIENT: sent %d bytes to %s\n", numbytes, server);
			last_message = message;

			//WAITING FOR ACKNOWLEDGEMENT - WRQ
			int times;
			addr_len = sizeof their_addr;
			for(times = 0; times <= MAX_TRIES; ++times) {
				if(times == MAX_TRIES){// reached max no. of tries
					printf("CLIENT: MAX NUMBER OF TRIES REACHED\n");
					exit(1);
				}

				// checking if timeout has occurred or not
				numbytes = check_timeout(sockfd, buf, their_addr, addr_len);
				if(numbytes == -1){//error
					perror("CLIENT: recvfrom");
					exit(1);
				} else if (numbytes == -2) {//timeout
					printf("CLIENT: try no. %d\n", times+1);
					int temp_bytes;
					if((temp_bytes = sendto(sockfd, last_message, strlen(last_message), 0, p->ai_addr, p->ai_addrlen)) == -1){
						perror("CLIENT ACK: sendto");
						exit(1);
					}
					printf("CLIENT: sent %d bytes AGAIN\n", temp_bytes);
					continue;
				} else if(numbytes > 5) {
					if(buf[0]=='0' && buf[1]=='5'){
		 				fprintf(stderr, "CLIENT: got error packet: %s\n", buf);
		 				exit(1);
		 			}
				} else { //valid
					break;
				}
			}
			printf("CLIENT: got packet from %s\n", inet_ntop(their_addr.ss_family, get_in_addr((struct sockaddr *)&their_addr), s, sizeof s));
			printf("CLIENT: packet is %d bytes long\n", numbytes);
			buf[numbytes] = '\0';
			printf("CLIENT: packet contains \"%s\"\n", buf);

			//WRITING NEW FILE
			if(buf[0] == '0' && buf[1 ]== '4') {

				printf("\n\n---Start writing the file, use TAB to end---\n\n");
				char file_buf[100];
				scanf("%[^\t]", file_buf);

				//SENDING FILE - DATA PACKET
				char *t_msg = (char *)malloc(strlen(file_buf)+1);
     			strcpy(t_msg, file_buf);
				
				if((numbytes = sendto(sockfd, t_msg, strlen(t_msg), 0, p->ai_addr, p->ai_addrlen)) == -1){
					perror("CLIENT: sendto");
					exit(1);
				}
				printf("CLIENT: sent %d bytes to %s\n", numbytes, server);
				last_message = t_msg;

				//WAITING FOR ACKNOWLEDGEMENT - DATA PACKET
				int times;
				for(times = 0; times <= MAX_TRIES; ++times){
					if(times == MAX_TRIES){
						printf("CLIENT: MAX NUMBER OF TRIES REACHED\n");
						exit(1);
					}

					numbytes = check_timeout(sockfd, buf, their_addr, addr_len);
					if(numbytes == -1) {//error
						perror("CLIENT: recvfrom");
						exit(1);
					} else if(numbytes == -2) {//timeout
						printf("CLIENT: try no. %d\n", times+1);
						int temp_bytes;
						if((temp_bytes = sendto(sockfd, last_message, strlen(last_message), 0, p->ai_addr, p->ai_addrlen)) == -1){
							perror("CLIENT ACK: sendto");
							exit(1);
						}
						printf("CLIENT: sent %d bytes AGAIN\n", temp_bytes);
						continue;
					} else { //valid
						break;
					}
				}
				printf("CLIENT: got packet from %s\n", inet_ntop(their_addr.ss_family, get_in_addr((struct sockaddr *)&their_addr), s, sizeof s));
				printf("CLIENT: packet is %d bytes long\n", numbytes);
				buf[numbytes] = '\0';
				printf("CLIENT: packet contains \"%s\"\n", buf);

				if(buf[0] == '0' && buf[1] == '5') {//if error packet received
					fprintf(stderr, "CLIENT: got error packet: %s\n", buf);
					exit(1);
				}

			} else {//some bad packed received
				fprintf(stderr,"CLIENT ACK: expecting but got: %s\n", buf);
				exit(1);
			}
		} 
		//UPLOAD EXISTING FILE========================================================================================================
		else if(choice == 2) {
			
			printf("\nEnter file name: ");
			scanf("%s", file_name);
			char *file = (char *)malloc(strlen(file_name)+1);
			strcpy(file, file_name);

			//SENDING WRQ
			char *message = make_wrq(file);
			char *last_message;
			if((numbytes = sendto(sockfd, message, strlen(message), 0, p->ai_addr, p->ai_addrlen)) == -1){
				perror("CLIENT: sendto");
				exit(1);
			}
			printf("CLIENT: sent %d bytes to %s\n", numbytes, server);
			last_message = message;

			//WAITING FOR ACKNOWLEDGEMENT - WRQ
			int times;
			addr_len = sizeof their_addr;
			for(times=0;times<=MAX_TRIES;++times){
				if(times == MAX_TRIES){// reached max no. of tries
					printf("CLIENT: MAX NUMBER OF TRIES REACHED\n");
					exit(1);
				}

				// checking if timeout has occurred or not
				numbytes = check_timeout(sockfd, buf, their_addr, addr_len);
				if(numbytes == -1){//error
					perror("CLIENT: recvfrom");
					exit(1);
				} else if(numbytes == -2){//timeout
					printf("CLIENT: try no. %d\n", times+1);
					int temp_bytes;
					if((temp_bytes = sendto(sockfd, last_message, strlen(last_message), 0, p->ai_addr, p->ai_addrlen)) == -1){
						perror("CLIENT ACK: sendto");
						exit(1);
					}
					printf("CLIENT: sent %d bytes AGAIN\n", temp_bytes);
					continue;
				} else if(numbytes > 5) {
					if(buf[0]=='0' && buf[1]=='5'){
		 				fprintf(stderr, "CLIENT: got error packet: %s\n", buf);
		 				exit(1);
		 			}
				} else { //valid
					break;
				}
			}
			printf("CLIENT: got packet from %s\n", inet_ntop(their_addr.ss_family, get_in_addr((struct sockaddr *)&their_addr), s, sizeof s));
			printf("CLIENT: packet is %d bytes long\n", numbytes);
			buf[numbytes] = '\0';
			printf("CLIENT: packet contains \"%s\"\n", buf);

			if(buf[0]=='0' && buf[1]=='4') {

			FILE *fp = fopen(file, "rb");
			if(fp == NULL || access(file, F_OK) == -1){ //FILE NOT FOUND
				fprintf(stderr,"CLIENT: file '%s' does not exist\n", file);
				exit(1);
			}

			// calculating file size
			long size;
			size_t final;
   			fseek (fp, 0 , SEEK_END);
  	 		size = ftell (fp);
   			rewind (fp);

			char *buff = (char*) malloc (sizeof(char)*size);
    		if (buff == NULL) {fputs ("Memory error", stderr); exit (2);}

			// copying file into buffer
    		final = fread (buff, 1, size, fp);
    		if (final != size) {
        		fputs ("Reading error", stderr); 
        		exit (3);
    		}

			if((numbytes = sendto(sockfd, buff, strlen(buff), 0, p->ai_addr, p->ai_addrlen)) == -1){
					perror("CLIENT: sendto");
					exit(1);
			}
			printf("CLIENT: sent %d bytes to %s\n", numbytes, server);

			last_message = buff;

			//WAITING FOR ACKNOWLEDGEMENT - DATA PACKET
			int times;
			for(times=0;times<=MAX_TRIES;++times){
					if(times == MAX_TRIES){
						printf("CLIENT: MAX NUMBER OF TRIES REACHED\n");
						exit(1);
					}

					numbytes = check_timeout(sockfd, buf, their_addr, addr_len);
					if(numbytes == -1){//error
						perror("CLIENT: recvfrom");
						exit(1);
					} else if(numbytes == -2){//timeout
						printf("CLIENT: try no. %d\n", times+1);
						int temp_bytes;
						if((temp_bytes = sendto(sockfd, last_message, strlen(last_message), 0, p->ai_addr, p->ai_addrlen)) == -1){
							perror("CLIENT ACK: sendto");
							exit(1);
						}
						printf("CLIENT: sent %d bytes AGAIN\n", temp_bytes);
						continue;
					} else { //valid
						break;
					}
				}
			printf("CLIENT: got packet from %s\n", inet_ntop(their_addr.ss_family, get_in_addr((struct sockaddr *)&their_addr), s, sizeof s));
			printf("CLIENT: packet is %d bytes long\n", numbytes);
			buf[numbytes] = '\0';
			printf("CLIENT: packet contains \"%s\"\n", buf);

			if(buf[0]=='0' && buf[1]=='5'){//if error packet received
				fprintf(stderr, "CLIENT: got error packet: %s\n", buf);
				exit(1);
			}	
				free(buff);
				fclose(fp);
			} else {
				fprintf(stderr,"CLIENT ACK: expecting but got: %s\n", buf);
				exit(1);
			}
			
		} else {
			fprintf(stderr, "\nIncorrect choice!\n");
		}

	} else if (mychoice == 3) {
		fprintf(stdout, "\nClosing clinet... Goodbye!\n\n");
	} else { //INVALID REQUEST
		fprintf(stderr,"\nIncorrect choice!\n");
		exit(1);
	}
	//===========MAIN IMPLEMENTATION - ENDS===========

	
	freeaddrinfo(servinfo);
	close(sockfd);
	
	return 0;
}
