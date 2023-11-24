#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <errno.h>
#include <stdbool.h>

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
    uint16_t acc_permission;
    uint8_t segment_no;
    uint8_t length;
    uint8_t technology;
    uint32_t source_subscriber_no;
    uint16_t end_packet_id;
} Packet;

void handle_sigint(int sig) {
    close(sockfd);
    printf("\nSocket closed. Exiting now.\n");
    exit(0);
}

#include <string.h>

typedef struct {
    char subscriberNumber[13];  // "408-554-6805" 12 + 1 (ending) bytes
    int technology;
    int paid;
} DatabaseEntry;

bool parseDatabaseLine(const char *line, DatabaseEntry *entry) {
    return sscanf(line, "%s %d %d", entry->subscriberNumber, &entry->technology, &entry->paid) == 3;
}

int handleSubscriber(const char *subscriberNo, int technology, const char *filename) {
    FILE *file = fopen(filename, "r");
    if (!file) {
        perror("Unable to open the file");
        return false;
    }

    char line[100];
    DatabaseEntry entry;
    bool subscriberFound = false;
    bool subscriberPaid = false;
    bool techMatched = false;
    while (fgets(line, sizeof(line), file)) {
        if (parseDatabaseLine(line, &entry)) { // read the line successfully
            if (strcmp(entry.subscriberNumber, subscriberNo) == 0) {
                subscriberFound = true;
                subscriberPaid = (entry.paid != 0);
                techMatched = (entry.technology == technology);
                break;
            }
        }
    }

    fclose(file);
    if (!subscriberFound) { // Subscriber not found
        return 0;
    } else if (!subscriberPaid) { // Number not paid
        return 2;
    } else if(!techMatched) { // Tech not matched
        return 3;
    } 
    return 1; // OK
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
        perror("Socket creation failed\n");
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
    while (1) {
        Packet received_packet;
        int n = recvfrom(sockfd, &received_packet, sizeof(received_packet), 0, (struct sockaddr *)&client_addr, &addr_len);
        if (n > 0) {
            char phoneNumber[11];
            char formattedNumber[13];
            sprintf(phoneNumber, "%u", received_packet.source_subscriber_no);
            sprintf(formattedNumber, "%c%c%c-%c%c%c-%c%c%c%c",
            phoneNumber[0], phoneNumber[1], 
            phoneNumber[2], phoneNumber[3], phoneNumber[4],
            phoneNumber[5], phoneNumber[6], phoneNumber[7], phoneNumber[8], phoneNumber[9]);
            int result = handleSubscriber(formattedNumber, received_packet.technology, "Verification_Database.txt");

            // prepare packet
            Packet out_packet;
            out_packet.start_packet_id = htons(START_OF_PACKET_ID);
            out_packet.client_id = received_packet.client_id;
            out_packet.segment_no = received_packet.segment_no;
            out_packet.length = received_packet.length;
            out_packet.technology = received_packet.technology;
            out_packet.source_subscriber_no = received_packet.source_subscriber_no;
            out_packet.end_packet_id = htons(END_OF_PACKET_ID);
            char message[100];
            if (result == 1) { //OK
                printf("Connect to subscriber: %u successfully\n", received_packet.source_subscriber_no);
                out_packet.acc_permission = htons(ACC_OK);
                sprintf(message, "ACC_OK response sent\n");
            } else if (result == 0) { // subscriber number not found
                out_packet.acc_permission = htons(NOT_EXIST);
                sprintf(message ,"NOT_EXIST response sent\n");
            } else if (result == 2) { // Number not paid
                out_packet.acc_permission = htons(NOT_PAID);
                sprintf(message, "NOT_PAID response sent\n");
            } else if (result == 3) { // Tech not matched
                out_packet.acc_permission = htons(TECH_NOT_MATCH);
                sprintf(message, "TECH_NOT_MATCH response sent\n");
            } else {
                sprintf(message, "Unexpected subscriber handle result\n");
            }
            sendto(sockfd, &out_packet, sizeof(out_packet), 0, (const struct sockaddr *)&client_addr, addr_len);
            printf("%s", message);
        } else if (n == 0) {
            printf("No packet received, but recvfrom returned successfully.\n");
        } else {
            perror("Error receiving data\n");
        }
    }
    close(sockfd);
    return 0;
}