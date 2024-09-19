#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdbool.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>

#define MAX_BUF_LEN 100
#define MAX_PEERS 10
#define HEARTBEAT_PORT "8080"
#define HEARTBEAT_MESSAGE "I AM ALIVE!"

struct socket {
    int sockfd;
    struct addrinfo *servinfo;
    char *host;
    char *port;
};

void *get_in_addr(struct sockaddr *sa);
bool compare_addr(struct addrinfo *ai, struct sockaddr_storage *ss);
int get_socket_index(struct socket **sockets, int n, struct sockaddr_storage *ss);
void get_hosts(char *file, char **hosts, int *n);
void bind_socket(char *port, struct socket* s);
void create_socket(char *host, char *port, struct socket* s);
struct socket *getsocket(struct socket **sockets, int sockfd, int n);

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "Usage: %s -h <hosts_file>\n", argv[0]);
        exit(1);
    }

    char **hosts = malloc((MAX_PEERS-1) * sizeof(char *));
    int n = 0;

    get_hosts(argv[2], hosts, &n);

    // Delay of 10 sec for all hosts to be ready
    // sleep(10);

    fd_set read_fds, write_fds, temp_read_fds, temp_write_fds;
    int max_fd = -1;
    struct socket *read_socket = malloc(sizeof(struct socket));
    struct socket **write_sockets = malloc(sizeof(struct socket*) * n);
    bool received_heartbeat[MAX_PEERS] = {false};
    int received_count = 0;

    FD_ZERO(&read_fds);
    FD_ZERO(&write_fds);
    
    bind_socket(HEARTBEAT_PORT, read_socket);
    int read_fd = read_socket->sockfd;
    FD_SET(read_fd, &read_fds);
    max_fd = read_fd;

    for (int i = 0; i < n; i++) {
        write_sockets[i] = malloc(sizeof(struct socket));
        create_socket(hosts[i], HEARTBEAT_PORT, write_sockets[i]);
        int write_fd = write_sockets[i]->sockfd;
        max_fd = (write_fd > max_fd) ? write_fd : max_fd;
        FD_SET(write_fd, &write_fds);
    }

    while (received_count < n) {
        temp_read_fds = read_fds;
        temp_write_fds = write_fds;

        if (select(max_fd+1, &temp_read_fds, &temp_write_fds, NULL, NULL) == -1) {
            perror("select");
            exit(4);
        }

        for (int i = 0; i <= max_fd; i++) {
            if (FD_ISSET(i, &temp_read_fds)) {
                struct sockaddr_storage their_addr;
                char buf[MAX_BUF_LEN];
                socklen_t addr_len = sizeof their_addr;
                char s[INET_ADDRSTRLEN];
                
                int numbytes = recvfrom(read_fd, buf, MAX_BUF_LEN-1, 0,
                    (struct sockaddr *)&their_addr, &addr_len);
                if (numbytes == -1) {
                    perror("recvfrom");
                    exit(1);
                }

                buf[numbytes] = '\0';
                int sender_index = get_socket_index(write_sockets, n, &their_addr);

                // fprintf(stdout, "listener: got packet from %s\n",
                //     inet_ntop(their_addr.ss_family,
                //               get_in_addr((struct sockaddr *)&their_addr),
                //               s, sizeof s));
                // fprintf(stdout, "listener: packet is %d bytes long\n", numbytes);
                // fprintf(stdout, "listener: packet contains \"%s\"\n", buf);

                if (strcmp(HEARTBEAT_MESSAGE, buf) == 0 && 
                    !received_heartbeat[sender_index]) {
                    received_count++;
                    fprintf(stderr, "Received heartbeat from %s\n", 
                            write_sockets[sender_index]->host);
                    received_heartbeat[sender_index] = true;
                }
            } else if (FD_ISSET(i, &temp_write_fds)) {
                struct socket *write_socket = getsocket(write_sockets, i, n);
                
                if (sendto(write_socket->sockfd, HEARTBEAT_MESSAGE, 
                           strlen(HEARTBEAT_MESSAGE), 0, 
                           write_socket->servinfo->ai_addr, 
                           write_socket->servinfo->ai_addrlen) == -1) {
                    perror("sendto");
                }
            }
        }
    }

    fprintf(stderr, "READY\n");

    free(read_socket);
    for (int i = 0; i < n; i++) {
        free(write_sockets[i]);
        free(hosts[i]);
    }
    free(write_sockets);
    free(hosts);

    return 0;
}

void *get_in_addr(struct sockaddr *sa) {
    return (sa->sa_family == AF_INET) 
        ? (void *)&(((struct sockaddr_in*)sa)->sin_addr)
        : (void *)&(((struct sockaddr_in6*)sa)->sin6_addr);
}

bool compare_addr(struct addrinfo *ai, struct sockaddr_storage *ss) {
    struct sockaddr_in *addr1 = (struct sockaddr_in *)ai->ai_addr;
    struct sockaddr_in *addr2 = (struct sockaddr_in *)ss;
    return (addr1->sin_addr.s_addr == addr2->sin_addr.s_addr);
}

int get_socket_index(struct socket **sockets, int n, struct sockaddr_storage *ss) {
    for (int i = 0; i < n; i++) {
        if (compare_addr(sockets[i]->servinfo, ss)) {
            return i;
        }
    }
    return -1;
}

void get_hosts(char *file, char **hosts, int *n) {
    FILE *fp;
    char *line = NULL;
    size_t len = 0;
    ssize_t n_chars;
    int i = 0;
    char hostname[128];

    gethostname(hostname, sizeof hostname);
    fprintf(stdout, "get_hosts: My hostname: %s\n", hostname);

    fp = fopen(file, "r");
    if (fp == NULL) {
        fprintf(stderr, "get_hosts: Error opening file %s\n", file);
        exit(1);
    }

    while ((n_chars = getline(&line, &len, fp)) != -1) {
        if (n_chars > 0 && line[n_chars - 1] == '\n') {
            line[n_chars - 1] = '\0';
            n_chars--;
        }

        if (strcmp(line, hostname) != 0) {
            hosts[i] = malloc((n_chars + 1) * sizeof(char));
            if (hosts[i] == NULL) {
                fprintf(stderr, "get_hosts: Memory allocation failed\n");
                exit(1);
            }
            strncpy(hosts[i], line, n_chars);
            hosts[i][n_chars] = '\0';
            i++;
        }
    }

    *n = i;
    fclose(fp);
    free(line);
}

void bind_socket(char *port, struct socket* s) {
    struct addrinfo hints, *servinfo, *p;
    int rv;

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    hints.ai_flags = AI_PASSIVE;

    if ((rv = getaddrinfo(NULL, port, &hints, &servinfo)) != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
        exit(1);
    }

    for(p = servinfo; p != NULL; p = p->ai_next) {
        s->sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (s->sockfd == -1) {
            perror("socket");
            continue;
        }
        if (bind(s->sockfd, p->ai_addr, p->ai_addrlen) == -1) {
            close(s->sockfd);
            perror("bind");
            continue;
        }
        break;
    }

    if (p == NULL) {
        fprintf(stderr, "Failed to bind socket\n");
        exit(1);
    }

    s->servinfo = p;
    s->host = NULL;
    s->port = port;
}

void create_socket(char *host, char *port, struct socket* s) {
    struct addrinfo hints, *servinfo, *p;
    int rv;

    memset(&hints, 0, sizeof hints);
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;

    if ((rv = getaddrinfo(host, port, &hints, &servinfo)) != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
        exit(1);
    }

    for(p = servinfo; p != NULL; p = p->ai_next) {
        s->sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol);
        if (s->sockfd == -1) {
            perror("socket");
            continue;
        }
        break;
    }

    if (p == NULL) {
        fprintf(stderr, "Failed to create socket\n");
        exit(1);
    }

    s->servinfo = p;
    s->host = host;
    s->port = port;
}

struct socket *getsocket(struct socket **sockets, int sockfd, int n) {
    for (int i = 0; i < n; i++) {
        if (sockets[i]->sockfd == sockfd)
            return sockets[i];
    }
    return NULL;
}