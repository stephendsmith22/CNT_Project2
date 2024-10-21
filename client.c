#include <stdio.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#define RCVBUFSIZE 512
#define SNDBUFSIZE 512

// Define message types
typedef enum {
    LIST = 1,
    DIFF = 2,
    PULL = 3,
    LEAVE = 4
} MessageType;

// Define the message header structure
typedef struct {
    MessageType type;
    uint32_t data_length;
} MessageHeader;

// Define the complete message structure
typedef struct {
    MessageHeader header;
    char data[256];
} Message;

// Function to handle LIST request
void send_list_request(int sockfd) {
    Message list_msg;
    list_msg.header.type = LIST;
    list_msg.header.data_length = 0;

    send(sockfd, &list_msg, sizeof(list_msg.header), 0);

    char buffer[RCVBUFSIZE];
    int received;
    printf("Files on the server:\n");
    while ((received = recv(sockfd, buffer, RCVBUFSIZE - 1, 0)) > 0) {
        buffer[received] = '\0';
        printf("%s", buffer);
        if (received < RCVBUFSIZE - 1) break;
    }
    printf("\n");
}

// Function to handle DIFF request
void send_diff_request(int sockfd) {
    Message diff_msg;
    diff_msg.header.type = DIFF;
    diff_msg.header.data_length = 0;

    send(sockfd, &diff_msg, sizeof(diff_msg.header), 0);

    char buffer[RCVBUFSIZE];
    int received;
    printf("Differences between client and server:\n");
    while ((received = recv(sockfd, buffer, RCVBUFSIZE - 1, 0)) > 0) {
        buffer[received] = '\0';
        printf("%s", buffer);
        if (received < RCVBUFSIZE - 1) break;
    }
    printf("\n");
}

// Function to handle PULL request
void send_pull_request(int sockfd, const char* filename) {
    Message pull_msg;
    pull_msg.header.type = PULL;
    pull_msg.header.data_length = strlen(filename);
    strcpy(pull_msg.data, filename);

    // Send the PULL request to the server
    send(sockfd, &pull_msg, sizeof(pull_msg.header) + pull_msg.header.data_length, 0);

    char buffer[RCVBUFSIZE];
    int received;

    // Check for error messages from the server
    if ((received = recv(sockfd, buffer, RCVBUFSIZE - 1, 0)) > 0) {
        buffer[received] = '\0';  // Null-terminate the string
        if (strstr(buffer, "Error:") != NULL) {
            printf("%s", buffer);  // Handle the error message
            close(sockfd);
            return;  // Exit the function if there was an error
        }
    }

    // Proceed to receive the file if no error was returned
    FILE *fp = fopen(filename, "wb");
    if (fp == NULL) {
        perror("Error opening file for writing");
        return;
    }

    printf("Receiving file '%s' from server...\n", filename);
    while ((received = recv(sockfd, buffer, RCVBUFSIZE, 0)) > 0) {
        fwrite(buffer, 1, received, fp);
        if (received < RCVBUFSIZE) break;  // Exit if less than RCVBUFSIZE was received
    }
    fclose(fp);
    printf("File '%s' downloaded successfully.\n", filename);
}


// Function to handle LEAVE request
void send_leave_request(int sockfd) {
    Message leave_msg;
    leave_msg.header.type = LEAVE;
    leave_msg.header.data_length = 0;

    send(sockfd, &leave_msg, sizeof(leave_msg.header), 0);
    printf("Client sent LEAVE message and is disconnecting...\n");
    close(sockfd);
}

int main(int argc, char *argv[]) {
    int clientSock;
    struct sockaddr_in serv_addr;

    if ((clientSock = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0) {
        perror("socket() failed");
        exit(1);
    }

    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    serv_addr.sin_port = htons(9091);

    if (connect(clientSock, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0) {
        perror("connect() failed");
        exit(1);
    }

    int client_left = 0;
    while (client_left == 0) {
        char input;
        char filename[256];
        printf("Enter '1' for LIST, '2' for DIFF, '3' for PULL, or '4' for LEAVE: ");
        scanf(" %c", &input);

        if (input == '1') {
            send_list_request(clientSock);
        } else if (input == '2') {
            send_diff_request(clientSock);
        } else if (input == '3') {
            printf("Enter the filename to PULL: ");
            scanf("%s", filename);
            send_pull_request(clientSock, filename);
        } else if (input == '4') {
            send_leave_request(clientSock);
            client_left = 1;  // This is the correct assignment
        } else {
            printf("Invalid input.\n");
        }

    }

    return 0;
}
