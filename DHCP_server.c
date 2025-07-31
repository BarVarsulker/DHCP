#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netinet/if_ether.h>
#include <netinet/in.h>
#include <net/if.h>
#include <sys/ioctl.h>
#include <linux/if_packet.h>
#include <time.h>
#include <ifaddrs.h>
#include <sys/types.h>
#include <netdb.h>
#include <pthread.h>

#define MAX_CLIENTS 10
#define BUFFER_SIZE 256
#define LEASE_TIME 120
#define INTERFACE "eth0"
#define SUBNET_MASK 24
const char* SERVER_IP = "192.8.1.1";

enum state {
	DISCOVER,
	OFFER,
	REQUEST,
	ACK,
	RENEW
};

typedef struct {
	char assigned_ip[16];
	char mac_address[18];
	int lease_time;
	int active;
	int wait;
} client_t;

client_t clients[MAX_CLIENTS];

void craft_ethernet_frame(unsigned char *frame, char *message) {
	memset(frame, 0xff, ETH_ALEN);
	memset(frame + ETH_ALEN, 0, ETH_ALEN);
	*((unsigned short *) (frame + 2 * ETH_ALEN) ) = htons(ETH_P_IP);
	memcpy(frame + ETH_HLEN, message, strlen(message));
}

void* time_handler() {
	int i;
	while(1) {
		sleep(1);
		for (i = 0; i < MAX_CLIENTS; i++) {
			if (clients[i].active == 1) {
				clients[i].lease_time = clients[i].lease_time - 1;
				if (clients[i].lease_time == 0) {
					clients[i].active = 0;
					strcpy(clients[i].mac_address, "00:00:00:00:00:00");
					clients[i].wait = 0;
					printf("the ip %s become available\n", clients[i].assigned_ip);
				}
			}
		}
	}
}

int main() {
	char mac_address[18];
	char ip[16];
	char result[BUFFER_SIZE * 3];
	int sockfd;
	struct ifreq if_idx;
	struct sockaddr_ll sa;
	unsigned char frame[ETH_FRAME_LEN];
	int recv_len;
	char *temp;
	char server_ip2[16];
	char stringcmp[BUFFER_SIZE];
	char *token;
	int j;
	int clear = 0;
	char curr[BUFFER_SIZE];
	pthread_t thread;

	if (pthread_create(&thread, NULL, time_handler, NULL) != 0) {
		perror("failed to create thread");
		exit(EXIT_FAILURE);
	}
	for (j = 0; j < MAX_CLIENTS; j++) {
		sprintf(clients[j].assigned_ip, "192.8.1.%d", j + 2);
		sprintf(clients[j].mac_address, "00:00:00:00:00:00");
		clients[j].lease_time = 0;
		clients[j].active = 0;
		clients[j].wait = 0;
	}

	if ((sockfd = socket(AF_PACKET, SOCK_RAW, htons(ETH_P_ALL))) == -1) {
		perror("error in socket in DISCOVER");
		exit(EXIT_FAILURE);
	}
	strncpy(if_idx.ifr_name, INTERFACE, IFNAMSIZ - 1);
	if (ioctl(sockfd, SIOCGIFINDEX, &if_idx) == -1) {
		perror("ioctl DISCOVER failed");
		close(sockfd);
		exit(EXIT_FAILURE);
	}
	sa.sll_family = AF_PACKET;
	sa.sll_protocol = htons(ETH_P_ALL);
	sa.sll_ifindex = if_idx.ifr_ifindex;
	sa.sll_halen = ETH_ALEN;

	memset(sa.sll_addr, 0xff, ETH_ALEN);
	while (1) {
			if (bind(sockfd, (struct sockaddr *)&sa, sizeof(sa)) == -1) {
				perror("DISCOVER bind failed");
				close(sockfd);
				exit(EXIT_FAILURE);
			}
			recv_len = recv(sockfd, frame, ETH_FRAME_LEN, 0);
			if (recv_len == -1) {
				perror("main recv() failed");
				close(sockfd);
				exit(EXIT_FAILURE);
			}
			temp = frame + ETH_HLEN;
			strcpy(result, temp);
			printf("getting message: %s\n", result);
			token = strtok(result, " ");
			strcpy(curr, token);
			if (strcmp(curr, "DISCOVER") == 0) { //this part handle DISCOVER messages
				token = strtok(NULL, " ");
				strcpy(mac_address, token);
				token = strtok(NULL, " ");
				strcpy(ip, token);
				if (strcmp(ip, "0.0.0.0") == 0) {
					for (j = 0; j < MAX_CLIENTS; j++) {
						if (clients[j].active == 0) {
							if (clients[j].wait == 0) {
								sprintf(result, "OFFER %s %s %s ", mac_address, SERVER_IP, clients[j].assigned_ip);
								clients[j].wait = 1;
								clear = 1;
								strcpy(clients[j].mac_address, mac_address);
								break;
							}
						}
					}
				}
				else {
					for (j = 0; j < MAX_CLIENTS; j++) {
						if (strcmp(clients[j].assigned_ip, ip) == 0) {
							if (clients[j].active == 0) {
								if (clients[j].wait == 0) {
									clear = 1;
									sprintf(result, "OFFER %s %s %s ", mac_address, SERVER_IP, clients[j].assigned_ip);
									clients[j].wait = 1;
									strcpy(clients[j].mac_address, mac_address);
									break;
								}
							}
						}
					}
				}
				if (clear == 0) {
					for (j = 0; j < MAX_CLIENTS; j++) {
						if (clients[j].active == 0) {
							if (clients[j].wait == 0) {
								sprintf(result, "OFFER %s %s %s ", mac_address, SERVER_IP, clients[j].assigned_ip);
								clients[j].wait = 1;
								strcpy(clients[j].mac_address, mac_address);
								break;
							}
						}
					}
				}
				sprintf(result + strlen(result), "%d", SUBNET_MASK);
				craft_ethernet_frame(frame, result);
				if (sendto(sockfd, frame, ETH_HLEN + strlen(result), 0, (struct sockaddr *)&sa, sizeof(sa)) == -1) {
					perror("DISCOVER sendto() failed");
					close(sockfd);
					exit(EXIT_FAILURE);
				}
				printf("sending broadcast: %s\n", result);
			}
			if (strcmp(curr, "REQUEST") == 0) { //this part handle REQUEST messages
				token = strtok(NULL, " ");
				strcpy(mac_address, token);
				token = strtok(NULL, " ");
				token = strtok(NULL, " ");
				strcpy(ip, token);
				for (j = 0; j < MAX_CLIENTS; j++) {
					if (strcmp(ip, clients[j].assigned_ip) == 0) {
						if (strcmp(mac_address, clients[j].mac_address) == 0) {
							if (clients[j].active == 0) {
								if (clients[j].wait == 1) {
									clients[j].wait = 0;
									clients[j].active = 1;
									clients[j].lease_time = LEASE_TIME;
									sprintf(result, "ACK %s ", clients[j].mac_address);
									sprintf(result + strlen(result), "%d", clients[j].lease_time);
									craft_ethernet_frame(frame, result);
									if (sendto(sockfd, frame, ETH_HLEN + strlen(result), 0, (struct sockaddr *)&sa, sizeof(sa)) == -1) {
										perror("REQUEST sendto failed");
										close(sockfd);
										exit(EXIT_FAILURE);
									}
									printf("sending broadcast: %s\n", result);
									break;
								}
							}
						}
					}
				}
			}
			if (strcmp(curr, "RENEW") == 0) { // this part handle RENEW messages
				token = strtok(NULL, " ");
				strcpy(mac_address, token);
				token = strtok(NULL, " ");
				strcpy(ip, token);
				for (j = 0; j < MAX_CLIENTS; j++) {
					if (strcmp(ip, clients[j].assigned_ip) == 0) {
						if (strcmp(mac_address, clients[j].mac_address) == 0) {
							clients[j].lease_time = clients[j].lease_time + LEASE_TIME;
							break;
						}
					}
				}
			}
	}
	return 0;
}
