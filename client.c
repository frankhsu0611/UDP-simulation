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
#define MAX_PACKET_SIZE 255
#define DATA_PACKET_TYPE 0xFFF1
#define ACK_PACKET_TYPE 0xFFF2
#define REJECT_PACKET_TYPE 0xFFF3
#define SERVER_PORT 65529
#define MAX_RETRIES 3
#define REJECT_CODE_OUT_OF_SEQUENCE 0xFFF4
#define REJECT_CODE_MISMATCH_LENGTH 0xFFF5
#define REJECT_CODE_MISSING_END_OF_PACKET 0xFFF6
#define REJECT_CODE_DUPLICATE_PACKET 0xFFF7

// dataPacket with packed format (without paddings)
typedef struct __attribute__((packed)){
    uint16_t start_packet_id;
    uint8_t client_id;
    uint16_t type;
    uint8_t segment_no;
    uint8_t length;
    char payload[255];
    uint16_t end_packet_id;
} DataPacket;


typedef struct __attribute__((packed)){
    uint16_t start_packet_id;
    uint8_t client_id;
    uint16_t type;
    uint16_t subcode;
    uint8_t received_segment_no;
    uint16_t end_packet_id;
} ResponsePacket;



int sendDataWithRetry(int sockfd, struct sockaddr_in server_addr, DataPacket packet) {
    int attemp = 1;
    int response_received = 0;
    ResponsePacket response;
    socklen_t addr_len = sizeof(server_addr);
    fd_set readfds;
    struct timeval tv;

    while (attemp <= MAX_RETRIES && !response_received) {
        // Send data packet
        printf("Send attempt %d out of 3 for segment %u\n", attemp, packet.segment_no);
        int send_success = sendto(sockfd, &packet, sizeof(packet), 0, (const struct sockaddr *)&server_addr, sizeof(server_addr));
        if (send_success) {
            printf("*************** Packet info *********************\n");
            printf("Start of Packet Identifier: %x\n", ntohs(packet.start_packet_id));
            printf("Client ID: %u\n", packet.client_id);
            printf("Segment No: %u\n", packet.segment_no);
            printf("Length: %u\n", packet.length);
            printf("Payload: %s\n", packet.payload);
            printf("End of Packet Identifier: %x\n", ntohs(packet.end_packet_id));
            printf("*************************************************\n");
        } else {
            printf("Packet not sent \n");
        }

        // Clear the FD set and set the timeout each time before select
        FD_ZERO(&readfds); // Initialize file descripter set
        FD_SET(sockfd, &readfds); // monitor sockfd for packet reception 

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
            printf("No ACK received for segment %d, retrying...\n\n", packet.segment_no);
            attemp++;
        } else if (FD_ISSET(sockfd, &readfds)) {
            printf("Response received\n");
            response_received = 1;
            // packet received, process it
            recvfrom(sockfd, &response, sizeof(response), 0, (struct sockaddr *)&server_addr, &addr_len);
            // Check if the received packet is an ACK for the correct segment
            if (ntohs(response.type) == ACK_PACKET_TYPE && response.received_segment_no == packet.segment_no) {
                printf("ACK received for segment %d \n\n", packet.segment_no);
                break;
            } else {
                if (ntohs(response.subcode) == REJECT_CODE_OUT_OF_SEQUENCE){
                    printf("[Error]: Segment %d is out of sequence.\n\n", packet.segment_no);
                } else if (ntohs(response.subcode) == REJECT_CODE_MISMATCH_LENGTH) {
                    printf("[Error]: Mismatched packet length for segment %d.\n\n", packet.segment_no);
                } else if (ntohs(response.subcode) == REJECT_CODE_MISSING_END_OF_PACKET) {
                    printf("[Error]: Missing end of packet symbol in segment %d.\n\n", packet.segment_no);
                } else if (ntohs(response.subcode) == REJECT_CODE_DUPLICATE_PACKET) {
                    printf("[Error]: Segment %d is duplicated.\n\n", packet.segment_no);
                }
                break;
            }      
        }
    }

    if (!response_received) {
        printf("Server does not respond.\n\n");
        return 0;
    }
    return 1;
}


int main() {
    
    // Setup UDP socket
    int sockfd;
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
    DataPacket packets[5];
    // Sample 0 (correct)
    for (int i = 0; i < 5; ++i) {
        packets[i].start_packet_id = htons(START_OF_PACKET_ID);
        packets[i].client_id = CLIENT_ID;
        packets[i].type = htons(DATA_PACKET_TYPE);
        packets[i].segment_no = i;  // Segment number set as the loop index
        snprintf(packets[i].payload, MAX_PACKET_SIZE, "Hi! I am payload %d.", i); 
        packets[i].length = strlen(packets[i].payload); // Set the actual packet size including the end symbol 
        packets[i].end_packet_id = htons(END_OF_PACKET_ID);
    }
    // Send packets
    for (int i = 0; i < 5; ++i) {
        int sucess = sendDataWithRetry(sockfd, server_addr, packets[i]);
        if (sucess == 0) {
            return 0;
        }
    }

    //Sample 1 (out of sequence)
    // for (int i = 0; i < 5; ++i) {
    //     packets[i].start_packet_id = htons(START_OF_PACKET_ID);
    //     packets[i].client_id = CLIENT_ID;
    //     packets[i].type = htons(DATA_PACKET_TYPE);
    //     packets[i].segment_no = i;  // Segment number set as the loop index
    //     snprintf(packets[i].payload, MAX_PACKET_SIZE, "Hi! I am payload %d.", i); 
    //     packets[i].length = strlen(packets[i].payload); // Set the actual packet size including the end symbol 
    //     packets[i].end_packet_id = htons(END_OF_PACKET_ID);
    // }
    // for (int i = 0; i < 3; ++i) {
    //     sendDataWithRetry(sockfd, server_addr, packets[i]);
    // }
    // sendDataWithRetry(sockfd, server_addr, packets[4]);
    // for (int i = 3; i < 5; ++i) {
    //     sendDataWithRetry(sockfd, server_addr, packets[i]);
    // }

    // Sample 2 (mismathc length)
    // for (int i = 0; i < 5; ++i) {
    //     packets[i].start_packet_id = htons(START_OF_PACKET_ID);
    //     packets[i].client_id = CLIENT_ID;
    //     packets[i].type = htons(DATA_PACKET_TYPE);
    //     packets[i].segment_no = i;  // Segment number set as the loop index
    //     snprintf(packets[i].payload, MAX_PACKET_SIZE, "Hi! I am payload %d.", i); 
    //     packets[i].length = strlen(packets[i].payload); // Set the actual packet size including the end symbol 
    //     packets[i].end_packet_id = htons(END_OF_PACKET_ID);
    // }
    // snprintf(packets[4].payload, MAX_PACKET_SIZE, "Hi!Iampayload%d.", 4);
    // for (int i = 0; i < 5; ++i) {
    //     sendDataWithRetry(sockfd, server_addr, packets[i]);
    // }

    // Sample 3 (missing end of packet)
    // for (int i = 0; i < 5; ++i) {
    //     packets[i].start_packet_id = htons(START_OF_PACKET_ID);
    //     packets[i].client_id = CLIENT_ID;
    //     packets[i].type = htons(DATA_PACKET_TYPE);
    //     packets[i].segment_no = i;  // Segment number set as the loop index
    //     snprintf(packets[i].payload, MAX_PACKET_SIZE, "Hi! I am payload %d.", i); 
    //     packets[i].length = strlen(packets[i].payload); // Set the actual packet size including the end symbol 
    //     packets[i].end_packet_id = htons(END_OF_PACKET_ID);
    // }
    // packets[4].end_packet_id = 0xFFF0;
    // for (int i = 0; i < 5; ++i) {
    //     sendDataWithRetry(sockfd, server_addr, packets[i]);
    // }

    // Sample 4 (duplicated packets)
    // for (int i = 0; i < 5; ++i) {
    //     packets[i].start_packet_id = htons(START_OF_PACKET_ID);
    //     packets[i].client_id = CLIENT_ID;
    //     packets[i].type = htons(DATA_PACKET_TYPE);
    //     packets[i].segment_no = i;  // Segment number set as the loop index
    //     snprintf(packets[i].payload, MAX_PACKET_SIZE, "Hi! I am payload %d.", i); 
    //     packets[i].length = strlen(packets[i].payload); // Set the actual packet size including the end symbol 
    //     packets[i].end_packet_id = htons(END_OF_PACKET_ID);
    // }
    // for (int i = 0; i < 3; ++i) {
    //     sendDataWithRetry(sockfd, server_addr, packets[i]);
    // }
    // sendDataWithRetry(sockfd, server_addr, packets[2]);
    // for (int i = 3; i < 5; ++i) {
    //     sendDataWithRetry(sockfd, server_addr, packets[i]);
    // }

    close(sockfd);
    return 0;
}

