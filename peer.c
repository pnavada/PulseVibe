#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <errno.h>
#include <netdb.h>
#include <stdbool.h>
#include<poll.h>


#define MAXBUFLEN 100
#define max(x, y) (((x) > (y)) ? (x) : (y))
#define MAX_NO_HOSTS 4


void *get_in_addr(struct sockaddr *sa)
{
    if (sa->sa_family == AF_INET) {
        return &(((struct sockaddr_in*)sa)->sin_addr);
    }

    return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

struct sock {

    int sockfd;
    struct addrinfo *servinfo;
    char *host;
    char *port;

};


bool compare_addr(struct addrinfo *ai, struct sockaddr_storage *ss) {

        struct sockaddr_in *addr1 = (struct sockaddr_in *)ai->ai_addr;
        struct sockaddr_in *addr2 = (struct sockaddr_in *)ss;
        
        return (addr1->sin_addr.s_addr == addr2->sin_addr.s_addr);
}

int get_index(struct sock ** sockets, int n, struct sockaddr_storage *ss) {

    //printf("n: %d\n", n);

    for (int i = 0; i < n; i++) {
        if (compare_addr(sockets[i]->servinfo, ss)) {
           // printf("Hello\n");
            return i;
        }
    }

    return -1;

}


void get_hosts(char *file, char ** hosts, int *n) {

    FILE *fp;
    char *line = NULL;
    size_t len = 0;
    ssize_t n_chars;
    int i = 0;
    // Read the host name
    char hostname[128];
    gethostname(hostname, sizeof hostname);
    printf("My hostname: %s\n", hostname);

    fp = fopen(file, "r");
    if (fp == NULL) {
        fprintf(stderr, "Error opening file %s\n", file);
        return;
    }

    while ((n_chars = getline(&line, &len, fp)) != -1) {
        // Remove newline character if present
        if (n_chars > 0 && line[n_chars - 1] == '\n') {
            line[n_chars - 1] = '\0';
            n_chars--;
        }

        // printf("Read line: %s (length: %zd)\n", line, n_chars);

        if (strcmp(line, hostname) != 0) { // No need to send heartbeats to running host
            hosts[i] = malloc((n_chars + 1) * sizeof(char));  // +1 for null terminator
            if (hosts[i] == NULL) {
                fprintf(stderr, "Memory allocation failed\n");
                continue;
            }
            strncpy(hosts[i], line, n_chars);
            hosts[i][n_chars] = '\0';  // Ensure null-termination
            // printf("Added host: %s\n", hosts[i]);
            i++;
        }
    }

    *n = i;

    fclose(fp);

    free(line);  // Free the line buffer allocated by getline

}


void bind_socket(char *port, struct sock* s) {
    
    int sockfd;
	struct addrinfo hints, *servinfo, *p;
	int rv;

	memset(&hints, 0, sizeof hints);
	hints.ai_family = AF_INET; // set to AF_INET to use IPv4
	hints.ai_socktype = SOCK_DGRAM;
	hints.ai_flags = AI_PASSIVE; // use my IP

	if ((rv = getaddrinfo(NULL, port, &hints, &servinfo)) != 0) {
		fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
        return;
	}

	// loop through all the results and bind to the first we can
	for(p = servinfo; p != NULL; p = p->ai_next) {
		if ((sockfd = socket(p->ai_family, p->ai_socktype,
				p->ai_protocol)) == -1) {
			perror("peer: socket");
			continue;
        }
        // char ip[INET_ADDRSTRLEN];
        // printf("%d", sockfd);
        // printf("%d\n", p->ai_family);
        // struct sockaddr_in *sin = (struct sockaddr_in *)p->ai_addr;
        // inet_ntop(AF_INET, &(sin->sin_addr), ip, INET_ADDRSTRLEN);
        // printf("%s\n", ip);

		if (bind(sockfd, p->ai_addr, p->ai_addrlen) == -1) {
			close(sockfd);
			perror("peer: bind");
			continue;
		}

		break;
	}

	if (p == NULL) {
		fprintf(stderr, "peer: failed to bind socket\n");
	}

    s->sockfd = sockfd;
    s->servinfo = p;
    s->host = NULL;
    s->port = port;

}


void attach_socket(char *host, char *port, struct sock* s) {
    
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
			perror("peer: socket");
			continue;
        }
		break;
	}

	if (p == NULL) {
		fprintf(stderr, "peer: failed to bind socket\n");
	}

    s->sockfd = sockfd;
    s->servinfo = p;
    s->host = host;
    s->port = port;

}

struct sock * getsock(struct sock **sockets, int sockfd, int n) {

    for (int i = 0; i < n; i++) {
        if (sockets[i]->sockfd == sockfd)
            return sockets[i];
    }

    return NULL;

}


int main(int argc, char *argv[]) {

    char **hosts = malloc((MAX_NO_HOSTS-1) * sizeof(char *));
    int n = 0;

    get_hosts(argv[2], hosts, &n);
    // printf("n: %d\n", n);

    fd_set read_fds;
    fd_set write_fds;
    int max_fd = -1;
    struct sock *read_socket = malloc(sizeof(struct sock));
    struct sock **write_sockets = malloc(sizeof(struct sock *) * n);
    char beat_msg[] = "I AM ALIVE";
    bool rbeat[MAX_NO_HOSTS] = {false};

    FD_ZERO(&read_fds);
    FD_ZERO(&write_fds);
    
    bind_socket("8080", read_socket);
    int read_fd = read_socket->sockfd;
    FD_SET(read_fd, &read_fds);

    max_fd = max(max_fd, read_fd);

    for (int i = 0; i < n; i++) {
        write_sockets[i] = malloc(sizeof(struct sock));
        attach_socket(hosts[i], "8080", write_sockets[i]);
        int write_fd = write_sockets[i]->sockfd;
        max_fd = max(max_fd, write_fd);
        FD_SET(write_fd, &write_fds);
    }

    int recv_count = 0;

    fd_set t_read_fds;
    fd_set t_write_fds;

    int count = 0;

    for (;;) {

        if (recv_count == n)
            break;

        t_read_fds = read_fds;
        t_write_fds = write_fds;

        if (select(max_fd+1, &t_read_fds, &t_write_fds, NULL, NULL) == -1) {
            perror("select");
            exit(4);
        }

        for(int i = 0; i <= max_fd; i++) {

            // printf("%d\n", i);

            if (FD_ISSET(i, &t_read_fds)) {

                if (i == read_fd) { // Read
                    struct sockaddr_storage their_addr;
                    int numbytes;
                    char buf[MAXBUFLEN];
                    socklen_t addr_len = sizeof their_addr;
                    char s[INET_ADDRSTRLEN];
                    
                    if ((numbytes = recvfrom(read_fd, buf, MAXBUFLEN-1 , 0,
                        (struct sockaddr *)&their_addr, &addr_len)) == -1) {
                        perror("recvfrom");
                        exit(1);
                    }

                    // printf("peer: got packet from %s\n",
                    // inet_ntop(their_addr.ss_family,
                        // get_in_addr((struct sockaddr *)&their_addr),
                        // s, sizeof s));
                    // printf("peer: packet is %d bytes long\n", numbytes);
                    buf[numbytes] = '\0';
                    // printf("peer: packet contains \"%s\"\n", buf); 

                    int p = get_index(write_sockets, n, &their_addr);
                    // printf("Index: %d\n", p);

                    if (!rbeat[p]) {
                        recv_count++;
                        printf("peer: got packet from %s\n", write_sockets[p]->host);
                        rbeat[p] = true;
                    }

                } 

                } 
                else if (FD_ISSET(i, &t_write_fds)) {
                    
                    struct sock *write_socket = getsock(write_sockets, i, n);
                    
                    if (sendto(write_socket->sockfd, 
                        beat_msg, strlen(beat_msg), 
                        0, 
                        write_socket->servinfo->ai_addr, 
                        write_socket->servinfo->ai_addrlen) == -1) {
                        perror("peer: sendto");
                    }
                }

            }
        }

    }

