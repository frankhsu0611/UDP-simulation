#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <errno.h>

#define START_OF_PACKET_ID 0xFFFF
#define END_OF_PACKET_ID 0xFFFF
#define CLIENT_ID 0xFF
#define SERVER_PORT 65520// to-do
#define MAX_RETRIES 3
#define SOURCE_SUBSCRIBER_NO 0xFFFFFFFF
#define TECH_2G 0x02
#define TECH_3G 0x03
#define TECH_4G 0x04
#define TECH_5G 0x05
#define TECH_NOT_MATCH 0xFFF7
#define REQUEST 0xFFF8
#define NOT_PAID 0xFFF9
#define NOT_EXIST 0xFFFA
#define ACC_OK 0xFFFB


int sockfd;

typedef struct __attribute__((packed)){
    uint16_t start_packet_id;
    uint8_t client_id;
    uint16_t type;
    uint8_t segment_no;
    uint8_t length;
    uint8_t technology;
    uint32_t source_subscriber_no;
    uint16_t end_packet_id;
} Packet;


void sendDataWithRetry(int segment_no, int sockfd, struct sockaddr_in server_addr, Packet request) {
    int attemp = 1;
    int connect_success = 0;
    int reponse_received = 0;
    Packet response;
    socklen_t addr_len = sizeof(server_addr);
    fd_set readfds;
    struct timeval tv;

    while (attemp <= MAX_RETRIES && !connect_success) {
        // Send data packet
        printf("Send attempt %d out of 3\n", attemp);
        int send_success = sendto(sockfd, &request, sizeof(request), 0, (const struct sockaddr *)&server_addr, sizeof(server_addr));
        if (send_success) {
            printf("*************** Packet info *********************\n");
            printf("Start of Packet Identifier: %x\n", ntohs(request.start_packet_id));
            printf("Client ID: %u\n", request.client_id);
            printf("Type: %x\n", htons(request.type));
            printf("Segment No: %u\n", request.segment_no);
            printf("Length: %u\n", request.length);
            printf("Technology: %u\n", request.technology);
            printf("End of Packet Identifier: %x\n", htons(request.end_packet_id));
            printf("*************************************************\n");
        } else {
            printf("Packet not sent \n");
        }

        // Clear the FD set and set the timeout each time before select
        FD_ZERO(&readfds);
        FD_SET(sockfd, &readfds);

        // Set timeout to 3 seconds
        tv.tv_sec = 3;
        tv.tv_usec = 0;

        // Wait for an ACK or timeout
        int activity = select(sockfd + 1, &readfds, NULL, NULL, &tv);

        if (activity < 0) {
            if (errno != EINTR) { // interrupted due to select error
                printf("Select error.\n");
            } else { // interrupted not due to other system call
                printf("Interrupted, retrying");
                continue;
            }
        }
        if (activity == 0) {
            // Timeout occurred
            printf("No response received for segment %d, retrying...\n\n", segment_no);

            attemp++;
        } else if (FD_ISSET(sockfd, &readfds)) {
            // reponse received, process it
            recvfrom(sockfd, &response, sizeof(response), 0, (struct sockaddr *)&server_addr, &addr_len);
            reponse_received += 1;
            // Check if the received packet is an ACK for the correct segment
            if (ntohs(response.type) == ACC_OK 
            && ntohl(response.source_subscriber_no == SOURCE_SUBSCRIBER_NO)) {
                connect_success = 1;
                printf("Connect Successfully\n");
            } else if (ntohs(response.type) == NOT_PAID) {
                printf("Subscriber_no: %u not paid\n", SOURCE_SUBSCRIBER_NO);
            } else if (ntohs(response.type) == NOT_EXIST) {
                printf("Subscriber_no: %u does not exist.\n", SOURCE_SUBSCRIBER_NO);
            } else if (ntohs(response.type) == TECH_NOT_MATCH) {
                printf("Subscriber_no: %u does not support requesting technology\n", SOURCE_SUBSCRIBER_NO);
            } else {
                printf("Unexpected response error\n");
            }
            break;
        }
    }

    if (reponse_received == 0) {
        printf("Server does not respond.\n");
    }
}

int main() {
    // Setup UDP socket
    struct sockaddr_in server_addr;

    sockfd = socket(AF_INET, SOCK_DGRAM, 0);

    if (sockfd < 0) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);
    server_addr.sin_addr.s_addr = INADDR_ANY;  // Replace with actual server IP

    // Create packets
    Packet packet;
    // Sample 0 (correct)
    packet.start_packet_id = htons(START_OF_PACKET_ID);
    packet.client_id = CLIENT_ID;
    packet.type = htons(REQUEST);
    packet.segment_no = 1;  // Segment number set as the loop index
    packet.length = 0x05; // for uint_8 + uint_32
    packet.technology = TECH_2G;
    packet.source_subscriber_no = htonl(SOURCE_SUBSCRIBER_NO);
    packet.end_packet_id = htons(END_OF_PACKET_ID);
    // Send packets
    sendDataWithRetry(1, sockfd, server_addr, packet);

    close(sockfd);
    return 0;
}