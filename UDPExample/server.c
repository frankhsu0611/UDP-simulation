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
#define REJECT_CODE_OUT_OF_SEQUENCE 0xFFF4
#define REJECT_CODE_MISMATCH_LENGTH 0xFFF5
#define REJECT_CODE_MISSING_END_OF_PACKET 0xFFF6
#define REJECT_CODE_DUPLICATE_PACKET 0xFFF7
#define SERVER_PORT 65529// to-do
#define MAX_RETRIES 3
#define EXPECTED_PACKETS 5


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



int sockfd;
int next_segment;

void handle_sigint(int sig) {
    close(sockfd);
    printf("Socket closed. Exiting now.\n");
    exit(0);
}


int main() {
    // Setup signal handler
    signal(SIGINT, handle_sigint);


    // Setup UDP socket
    struct sockaddr_in server_addr, client_addr;
    int opt = 1;

    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    if (sockfd < 0) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(sockfd, (const struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Bind failed");
        exit(EXIT_FAILURE);
    }
    socklen_t addr_len = sizeof(client_addr);

    // Receive data and send ACK
    next_segment = 0;
    while (1) {
        DataPacket received_packet;
        int n = recvfrom(sockfd, &received_packet, sizeof(received_packet), 0, (struct sockaddr *)&client_addr, &addr_len);
        if (n > 0) {
            if (received_packet.segment_no > next_segment) { 
                // Prepare out of sequecne reject packet
                ResponsePacket rej_packet;
                rej_packet.start_packet_id = htons(START_OF_PACKET_ID);
                rej_packet.client_id = received_packet.client_id;
                rej_packet.type = htons(REJECT_PACKET_TYPE);
                rej_packet.subcode = htons(REJECT_CODE_OUT_OF_SEQUENCE);
                rej_packet.received_segment_no =  received_packet.segment_no;
                rej_packet.end_packet_id = htons(END_OF_PACKET_ID);

                // Send REJ
                printf("[Server Error]: Segment %d is out of sequence. Expecting %d\n", received_packet.segment_no, next_segment);
                int send_success = sendto(sockfd, &rej_packet, sizeof(rej_packet), 0, (const struct sockaddr *)&client_addr, addr_len);
                if (send_success > 0) {
                    printf("REJ sent\n");
                }
            } else if (received_packet.length != strlen(received_packet.payload)){ 
                ResponsePacket rej_packet;
                rej_packet.start_packet_id = htons(START_OF_PACKET_ID);
                rej_packet.client_id = received_packet.client_id;
                rej_packet.type = htons(REJECT_PACKET_TYPE);
                rej_packet.subcode = htons(REJECT_CODE_MISMATCH_LENGTH);
                rej_packet.received_segment_no =  received_packet.segment_no;
                rej_packet.end_packet_id = htons(END_OF_PACKET_ID);

                // Send REJ
                printf("[Server Error]: Mismatched packet length for segment %d.\n", received_packet.segment_no);
                int send_success = sendto(sockfd, &rej_packet, sizeof(rej_packet), 0, (const struct sockaddr *)&client_addr, addr_len);
                if (send_success > 0) {
                    printf("REJ sent\n");
                }
            } else if (received_packet.end_packet_id != htons(END_OF_PACKET_ID)) {
                ResponsePacket rej_packet;
                rej_packet.start_packet_id = htons(START_OF_PACKET_ID);
                rej_packet.client_id = received_packet.client_id;
                rej_packet.type = htons(REJECT_PACKET_TYPE);
                rej_packet.subcode = htons(REJECT_CODE_MISSING_END_OF_PACKET);
                rej_packet.received_segment_no =  received_packet.segment_no;
                rej_packet.end_packet_id = htons(END_OF_PACKET_ID);

                 // Send REJ
                printf("[Server Error]: Missing end of packet symbol in segment %d.\n", received_packet.segment_no);
                int send_success = sendto(sockfd, &rej_packet, sizeof(rej_packet), 0, (const struct sockaddr *)&client_addr, addr_len);
                if (send_success > 0) {
                    printf("REJ sent\n");
                }
            } else if (received_packet.segment_no < next_segment) {
                ResponsePacket rej_packet;
                rej_packet.start_packet_id = htons(START_OF_PACKET_ID);
                rej_packet.client_id = received_packet.client_id;
                rej_packet.type = htons(REJECT_PACKET_TYPE);
                rej_packet.subcode = htons(REJECT_CODE_DUPLICATE_PACKET);
                rej_packet.received_segment_no =  received_packet.segment_no;
                rej_packet.end_packet_id = htons(END_OF_PACKET_ID);

                // Send REJ
                printf("[Server Error]: Segment %d is duplicated. Expecting %d\n", received_packet.segment_no, next_segment);
                int send_success = sendto(sockfd, &rej_packet, sizeof(rej_packet), 0, (const struct sockaddr *)&client_addr, addr_len);
                if (send_success > 0) {
                    printf("REJ sent\n");
                }
            } else {
                printf("[Success]: Segment %d received.\n", received_packet.segment_no);
                // Prepare ACK packet
                ResponsePacket ack_packet;
                ack_packet.start_packet_id = htons(START_OF_PACKET_ID);
                ack_packet.client_id = received_packet.client_id;
                ack_packet.type = htons(ACK_PACKET_TYPE);
                ack_packet.received_segment_no = received_packet.segment_no;
                ack_packet.end_packet_id = htons(END_OF_PACKET_ID);
                // Send ACK
                next_segment += 1;
                int send_success = sendto(sockfd, &ack_packet, sizeof(ack_packet), 0, (const struct sockaddr *)&client_addr, addr_len);
                if (send_success > 0) {
                    printf("ACK sent\n");
                }
                if (next_segment == EXPECTED_PACKETS) {
                    printf("All packet received. Exit \n");
                    close(sockfd);
                    return 0;
                }
            }

            
        } else if (n == 0) {
            printf("No data received, but recvfrom returned successfully.\n");
        } else {
            perror("Error receiving data\n");
        }
    }
    close(sockfd);
    return 0;
}



