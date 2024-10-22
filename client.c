#include <stdio.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <openssl/md5.h> 
#include <openssl/evp.h>

#define RCVBUFSIZE 512

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

char client_dir[] = "./client";  // Directory where client stores music files

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

void compute_file_hash(const char *file_path, char *hash_str) {
    unsigned char hash[MD5_DIGEST_LENGTH];
    FILE *file = fopen(file_path, "rb");
    if (!file) {
        strcpy(hash_str, "error");
        return;
    }

    MD5_CTX md5_ctx;
    MD5_Init(&md5_ctx);
    char buffer[1024];
    size_t bytes;
    while ((bytes = fread(buffer, 1, sizeof(buffer), file)) != 0) {
        MD5_Update(&md5_ctx, buffer, bytes);
    }
    MD5_Final(hash, &md5_ctx);
    fclose(file);

    for (int i = 0; i < MD5_DIGEST_LENGTH; i++) {
        sprintf(&hash_str[i * 2], "%02x", hash[i]);
    }
    hash_str[MD5_DIGEST_LENGTH * 2] = '\0';  // Null-terminate the hash string
}

void send_diff_request(int sockfd, const char *client_dir) {
    Message diff_msg;
    diff_msg.header.type = DIFF;
    diff_msg.header.data_length = 0;  // No additional data for the request

    // Send the DIFF request to the server
    send(sockfd, &diff_msg, sizeof(diff_msg.header), 0);

    // Get the client's files and hashes
    char client_file_hashes[RCVBUFSIZE * 10] = "";
    DIR *dir = opendir(client_dir);
    if (!dir) {
        perror("opendir() failed");
        return;
    }

    struct dirent *dir_entry;
    while ((dir_entry = readdir(dir)) != NULL) {
        if (strcmp(dir_entry->d_name, ".") != 0 && strcmp(dir_entry->d_name, "..") != 0) {
            char file_path[512];
            snprintf(file_path, sizeof(file_path), "%s/%s", client_dir, dir_entry->d_name);

            char hash_str[MD5_DIGEST_LENGTH * 2 + 1];
            compute_file_hash(file_path, hash_str);

            snprintf(client_file_hashes + strlen(client_file_hashes), sizeof(client_file_hashes) - strlen(client_file_hashes),
                     "%s %s\n", dir_entry->d_name, hash_str);
        }
    }
    closedir(dir);

    // Send the client file hashes to the server
    send(sockfd, client_file_hashes, strlen(client_file_hashes), 0);
    
    // Receive server's file list and hashes
    char server_files[RCVBUFSIZE * 10] = "";
    int received = recv(sockfd, server_files, sizeof(server_files) - 1, 0);
    server_files[received] = '\0';

    // Debugging: Print server files
    // printf("Server Files and Hashes:\n%s\n", server_files);

    // Identify files that are missing from the client
    printf("Files missing from client:\n");
    char *server_file = strtok(server_files, "\n");
    while (server_file != NULL) {
        char server_filename[256], server_hash[MD5_DIGEST_LENGTH * 2 + 1];
        sscanf(server_file, "%s %s", server_filename, server_hash);

        // Check if the server file's hash exists in the client's hashes
        if (strstr(client_file_hashes, server_filename) == NULL) {
            printf(server_filename, "\n");
        }

        server_file = strtok(NULL, "\n");
    }
}



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
    } else {
        // Now expect to receive the file size
        long file_size;
        recv(sockfd, &file_size, sizeof(file_size), 0);
        printf("Receiving file '%s' (size: %ld bytes) from server...\n", filename, file_size);

        // Create a valid path for saving the file
        char file_path[512];
        snprintf(file_path, sizeof(file_path), "%s/%s", client_dir, filename); // Save to client directory

        // Proceed to receive the file
        FILE *fp = fopen(file_path, "wb");
        if (fp == NULL) {
            perror("Error opening file for writing");
            return;
        }

        long total_received = 0;
        while (total_received < file_size) {
            received = recv(sockfd, buffer, RCVBUFSIZE, 0);
            if (received < 0) {
                perror("Error receiving file data");
                fclose(fp);
                return;
            }
            fwrite(buffer, 1, received, fp);
            total_received += received;
        }
        fclose(fp);
        printf("File '%s' downloaded successfully to '%s'.\n", filename, file_path);
    }
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
    serv_addr.sin_port = htons(8092);

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
            send_diff_request(clientSock, client_dir);
        } else if (input == '3') {
            printf("Enter the filename to PULL: ");
            scanf("%s", filename);
            send_pull_request(clientSock, filename);
        } else if (input == '4') {
            send_leave_request(clientSock);
            client_left = 1;  // Correct assignment for exit
        } else {
            printf("Invalid input.\n");
        }
    }

    return 0;
}
