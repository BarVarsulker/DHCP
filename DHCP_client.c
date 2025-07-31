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
#include <sys/types.h>
#include <ifaddrs.h>
#include <netdb.h>

#define INTERFACE "eth0"
#define BUFFER_SIZE 256

enum menu { //main menu
	CONNECT,
	EXIT
};
enum state { //DHCP process
	DISCOVER,
	OFFER,
	REQUEST,
	ACK,
	RENEW
};

/* This function is for getting the mac address of this pc */
void get_mac_address(char *mac_str) {
	char path[BUFFER_SIZE];
	FILE *fp;

	snprintf(path, sizeof(path), "/sys/class/net/%s/address", INTERFACE);

	fp = fopen(path, "r");
	if (fp == NULL) {
		perror("get_mac_address fopen()");
		exit(EXIT_FAILURE);
	}

	if (fgets(mac_str, 18, fp) == NULL) {
		perror("get_mac_address fgets()");
		fclose(fp);
		exit(EXIT_FAILURE);
	}

	fclose(fp);

	mac_str[strcspn(mac_str, "\n")] = 0;
}

void craft_ethernet_frame(unsigned char *frame, char *message) {
	memset(frame, 0xff, ETH_ALEN);
	memset(frame + ETH_ALEN, 0, ETH_ALEN);
	*((unsigned short *) (frame + 2 * ETH_ALEN) ) = htons(ETH_P_IP);
	memcpy(frame + ETH_HLEN, message, strlen(message));
}

int configure_interface(const char *ip_address, const char *submask) {
	char command[256];
	int ret;

	snprintf(command, sizeof(command), "sudo ip addr add %s/%s dev %s", ip_address, submask, INTERFACE);
	ret = system(command);
	if (ret != 0) {
		printf("configure ip failed");
		return -1;
	}
	printf("IP address %s/%s configured on %s.\n", ip_address, submask, INTERFACE);
	return 0;
}

int main() {
	enum menu me;
	enum state curr;
	char mac_str[18];
	char specific_ip[16] = "0.0.0.0";
	char result[BUFFER_SIZE * 3];
	int whileVar = 1;
	int choice;
	int currstate = 0; //if we are in the second switch
	int sockfd;
	struct ifreq if_idx;
	struct sockaddr_ll sa;
	int server_port = 67;
	struct sockaddr_in server_addr;
	unsigned char frame[ETH_FRAME_LEN];
	int recv_len;
	time_t timer, timref;
	char *temp;
	char stringcmp[BUFFER_SIZE];
	int check;
	char server_ip[16];
	char offer_ip[16];
	char subnet_mask[3];
	char *token;
	int lease_time;
	int lease = 0;
	int sent_result;
	int renew = 0;
	struct timeval timeout;

	get_mac_address(mac_str);
	printf("%s\n", mac_str);
	while (whileVar) {
		if (currstate == 0) {
			printf("1. connect to internet\n");
			printf("2. exit\n");
			printf("please enter your choice: ");
			scanf("%d", &choice);
			me = choice - 1;
		}
		switch(me) {
			case CONNECT:
				if (currstate == 0) {
					printf("1. i want specific ip \n");
					printf("2. i want any available ip \n");
					printf("please  enter your choice: ");
					scanf("%d", &choice);
					if (choice == 1) {
						printf("Enter your ip: ");
						scanf("%s", specific_ip);
						printf("your specific ip is: %s\n", specific_ip);
					}
					curr = DISCOVER;
					currstate = 1;
				}
				switch(curr) {
					case DISCOVER:
						snprintf(result, sizeof(result), "DISCOVER %s %s", mac_str, specific_ip);
						printf("Sending the Discover message: %s\n", result);
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
						timeout.tv_sec = 5;
						timeout.tv_usec = 0;
						if (setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout)) < 0) {
							perror("setsockopt failed");
							close(sockfd);
							exit(EXIT_FAILURE);
						}

						craft_ethernet_frame(frame, result);
						sa.sll_family = AF_PACKET;
						sa.sll_protocol = htons(ETH_P_ALL);
						sa.sll_ifindex = if_idx.ifr_ifindex;
						sa.sll_halen = ETH_ALEN;

						memset(sa.sll_addr, 0xff, ETH_ALEN);

						if (sendto(sockfd, frame, ETH_HLEN + strlen(result), 0, (struct sockaddr *)&sa, sizeof(sa)) == -1) {
							perror("DISCOVER sendto failed");
							close(sockfd);
							exit(EXIT_FAILURE);
						}
						strcpy(specific_ip, "0.0.0.0");
						curr = OFFER;
						break;
					case  OFFER:
						if (bind(sockfd, (struct sockaddr *)&sa, sizeof(sa)) == -1) {
							perror("OFFER bind failed");
							close(sockfd);
							exit(EXIT_FAILURE);
						}
						recv_len = recv(sockfd, frame, ETH_FRAME_LEN, 0);
						if (recv_len == -1) {
							curr = DISCOVER;
						}
						else {
							temp = frame + ETH_HLEN;
							strcpy(result, temp);
							sprintf(stringcmp, "OFFER %s", mac_str);
							printf("%s\n", stringcmp);
							check = strncmp(result, stringcmp, 23);
							if (check == 0)
								curr = REQUEST;
						}
						break;
					case REQUEST:
						token = strtok(result, " ");
						token = strtok(NULL, " ");
						token = strtok(NULL, " ");
						strcpy(server_ip, token);
						token = strtok(NULL, " ");
						strcpy(offer_ip, token);
						token = strtok(NULL, " ");
						strcpy(subnet_mask, token);
						subnet_mask[2] = '\0';
						printf("The offer values: \n");
						printf("Server IP: %s\n", server_ip);
						printf("Offer IP: %s\n", offer_ip);
						printf("Subnet Mask: %s\n", subnet_mask);
						sprintf(result, "REQUEST %s %s %s %s", mac_str, server_ip, offer_ip, subnet_mask);
						printf("Sending REQUEST message: %s\n", result);
						craft_ethernet_frame(frame, result);
						if (sendto(sockfd, frame, ETH_HLEN + strlen(result), 0, (struct sockaddr *)&sa, sizeof(sa)) == -1) {
							perror("REQUEST sendto() failed");
							close(sockfd);
							exit(EXIT_FAILURE);
						}
						curr = ACK;
						break;
					case ACK:
						if (bind(sockfd, (struct sockaddr *)&sa, sizeof(sa)) == -1) {
							perror("ACK bind() failed");
							close(sockfd);
							exit(EXIT_FAILURE);
						}
						recv_len = recv(sockfd, frame, ETH_FRAME_LEN, 0);
						if (recv_len == -1) {
							printf("didnt get ACK from the server\n");
							curr = DISCOVER;
						}
						else {
							temp = frame + ETH_HLEN;
							strcpy(result, temp);
							sprintf(stringcmp, "ACK %s", mac_str);
							printf("%s\n", stringcmp);
							check = strncmp(result, stringcmp, 21);
							if (check == 0) {
								configure_interface(offer_ip, subnet_mask);
								token = strtok(result, " ");
								token = strtok(NULL, " ");
								token = strtok(NULL, " ");
								lease_time = atoi(token);
								printf("Lease time: %d\n", lease_time);
								lease = 0;
								curr = RENEW;
							}
						}
						break;
					case RENEW:
						lease = lease_time;
						while (lease > 0) {
							sleep(1);
							lease = lease - 1;
							printf("time remaining to the connection: %d\n", lease);
							if (lease == lease_time / 2) {
								printf("1. ask for renew the ip\n");
								printf("2. dont renew the ip\n");
								scanf("%d", &check);
								if (check == 1) {
									sprintf(result, "RENEW %s %s", mac_str, offer_ip);
									printf("sending RENEW message: %s\n", result);
									craft_ethernet_frame(frame, result);
									if (sendto(sockfd, frame, ETH_HLEN + strlen(result), 0, (struct sockaddr *)&sa, sizeof(sa)) == -1) {
										perror("RENEW sendto() failed");
										close(sockfd);
										exit(EXIT_FAILURE);
									}
									lease = lease + lease_time;
								}
							}
						}
						close(sockfd);
						currstate = 0;
						printf("The lease time is end\n");
						check = system("sudo ifconfig eth0 0.0.0.0");
						if (check != 0) {
							perror("Failed to drop the IP address");
							exit(EXIT_FAILURE);
						}
						printf("IP address dropped successfully.\n");
						break;
					default:
						continue;
				}
				break;
			case EXIT:
				whileVar = 0;
				break;
			default:
				printf("Please Enter correct choice! \n");
				continue;
		}
	}
	return 0;
}
