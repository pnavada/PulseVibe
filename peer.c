#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include<poll.h>
#include <stdbool.h>

#define MAX_NO_HOSTS 5
#define ACK_PORT "8081"
#define BEAT_PORT "8080"

struct sock {

    int sockfd;
    struct addrinfo *servinfo;
    char *host;
    char *port;

};

void bind_socket(char *host, char *port, struct sock* s) {
    
    int sockfd;
	struct addrinfo hints, *servinfo, *p;
	int rv;

	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_INET; // set to AF_INET to use IPv4
	hints.ai_socktype = SOCK_DGRAM;

    if (host == NULL)
	    hints.ai_flags = AI_PASSIVE; // use my IP

	if ((rv = getaddrinfo(host, port, &hints, &servinfo)) != 0) {
		fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
        return;
	}

	// loop through all the results and bind to the first we can
	for(p = servinfo; p != NULL; p = p->ai_next) {
		if ((sockfd = socket(p->ai_family, p->ai_socktype,
				p->ai_protocol)) == -1) {
			perror("listener: socket");
			continue;
		}

		if (bind(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
			close(sockfd);
			perror("listener: bind");
			continue;
		}

		break;
	}

	if (p == NULL) {
		fprintf(stderr, "listener: failed to bind socket\n");
	}

    s->sockfd = sockfd;
    s->servinfo = p;
    s->host = host;
    s->port = port;

}

void get_hosts(char *file, char ** hosts, char *host) {

    FILE *fp;
    char *line = NULL;
    size_t line_size, n_chars;
    int i = 0;

    fp = fopen(file, "r");

    while ((n_chars = getline(&line, &line_size, fp)) != -1) {
        fprintf(stderr, "Host name: %s", line);
        line[line_size-1] = '\0';
        if (strcmp(line, host) != 0) { // No need to send heartbeats to running host
            hosts[i] = malloc(line_size * sizeof(char));
            memset(hosts[i], 0, line_size);
            hosts[i] = line;
            i += 1;
        }
    }

    fclose(fp);

    if (line)
        free(line);

}

struct sock * getsock(struct sock **sockets, int sockfd) {

    for (int i = 0; i < MAX_NO_HOSTS; i++) {
        if (sockets[i]->sockfd == sockfd)
            return sockets[i];
    }

    return NULL;

}

int getindex(struct sock **sockets, struct sock* socket) {

    for (int i = 0; i < MAX_NO_HOSTS; i++) {
        if (strcmp(sockets[i]->servinfo->ai_addr->sa_data,socket->servinfo->ai_addr->sa_data) == 0)
            return i;
    }

    return -1;

}

int getpfdsindex(struct pollfd *pfds, int fd_count, int sockfd) {

    for (int i = 0; i < fd_count; i++) {
        if (pfds[i].fd == sockfd)
            return i;
    }

    return -1;

}

// Remove an index from the set
// void del_from_pfds(struct pollfd pfds[], int i, int *fd_count)
// {
//     // Copy the one from the end over this one
//     pfds[i] = pfds[*fd_count-1];
//     (*fd_count)--;
// }

int main(int argc, char *argv[]) {

    char ** hosts = malloc(MAX_NO_HOSTS * sizeof(char *));
    int fd_count = 2 * MAX_NO_HOSTS + 2;
    struct pollfd *pfds = malloc(sizeof *pfds * fd_count);
    char ack_msg[] = "ACK";
    char beat_msg[] = "I AM ALIVE!";
    int ack_count=0, recv_count=0;
    int n = MAX_NO_HOSTS;
    bool rack[MAX_NO_HOSTS] = {false};
    bool rbeat[MAX_NO_HOSTS] = {false};

    get_hosts(argv[1], hosts, argv[2]);

    struct sock **sbeat_sockets = malloc(sizeof(struct sock *) * MAX_NO_HOSTS); 
    struct sock **sack_sockets = malloc(sizeof(struct sock *) * MAX_NO_HOSTS); 
    
    for (int i = 0; i < MAX_NO_HOSTS; i++) {
        bind_socket(hosts[i], BEAT_PORT, sbeat_sockets[i]);
        pfds[i].fd = sbeat_sockets[i]->sockfd;
        pfds[i].events = POLL_OUT;
    }

    for (int i = MAX_NO_HOSTS; i < 2 * MAX_NO_HOSTS; i++) {
        bind_socket(hosts[i-MAX_NO_HOSTS], ACK_PORT, sack_sockets[i-MAX_NO_HOSTS]);
        pfds[i].fd = sack_sockets[i]->sockfd;
        pfds[i].events = POLL_OUT;
    }

    struct sock *rbeat_socket = malloc(sizeof(struct sock *));
    bind_socket(NULL, BEAT_PORT, rbeat_socket);
    pfds[2*MAX_NO_HOSTS].fd = rbeat_socket->sockfd;
    pfds[2*MAX_NO_HOSTS].events = POLL_IN;

    struct sock *rack_socket = malloc(sizeof(struct sock *));
    bind_socket(NULL, ACK_PORT, rack_socket);
    pfds[2*MAX_NO_HOSTS+1].fd = rack_socket->sockfd;
    pfds[2*MAX_NO_HOSTS+1].events = POLL_IN;

    for (;;) {

        if (recv_count == n-1 && ack_count == n-1) {
            // Free all resources
            break;
        }

        int poll_count = poll(pfds, fd_count, -1);

        if (poll_count == -1) {
            perror("poll");
            exit(1);
        }

        // Run through the existing connections looking for data to read
        for(int i = 0; i < fd_count; i++) {

            if (pfds[i].revents & POLLIN) { // Receive data

                if (pfds[i].fd == rack_socket->sockfd) { // Received ack

                    int index = getindex(sack_sockets, rack_socket);
                    if (!rack[index]) {
                        rack[index] = true;
                        ack_count++;
                    }
                    
                    // Remove the send socket
                    // index = getpfdsindex(pfds, fd_count, sbeat_sockets[index]->sockfd);
                    // del_from_pfds(pfds, index, &fd_count);

                } else if (pfds[i].fd == rbeat_socket->sockfd) { // Received heartbeat
                    
                    int index = getindex(sbeat_sockets, rbeat_socket);
                    if (!rbeat[index]) {
                        rbeat[index] = true;
                        recv_count++;
                    }

                }

            } else if (pfds[i].revents & POLLOUT) { // Send data

                struct sock *sbeat_socket = getsock(sbeat_sockets, pfds[i].fd);
                
                if (sbeat_socket != NULL) { // Send heartbeat

                    if (sendto(sbeat_socket->sockfd, beat_msg, strlen(beat_msg), 0, sbeat_socket->servinfo->ai_addr, sbeat_socket->servinfo->ai_addrlen) == -1) {
                        perror("peer: sendto");
                    }

                } else{ // Send ack

                    struct sock *sack_socket = getsock(sack_sockets, pfds[i].fd);
                    int index = getindex(sack_sockets, sack_socket);
                     
                    if (rbeat[index]) {
                        if (sendto(sack_socket->sockfd, ack_msg, strlen(ack_msg), 0, sack_socket->servinfo->ai_addr, sack_socket->servinfo->ai_addrlen) == -1) {
                            perror("peer: sendto");
                        } 
                        // else {
                        //     del_from_pfds(pfds, i, &fd_count);
                        // }
                    }

                }

            }

        }

    }

}