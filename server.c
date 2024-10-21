#include <stdio.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <dirent.h>
#include <pthread.h>

#define RCVBUFSIZE 512

typedef enum {
    LIST = 1,
    DIFF = 2,
    PULL = 3,
    LEAVE = 4
} MessageType;

typedef struct {
    MessageType type;
    uint32_t data_length;
} MessageHeader;

typedef struct {
    MessageHeader header;
    char data[256];
} Message;

// Function to handle LIST request
void handle_client_list(int client_socket) {
    struct dirent *dir_entry;
    DIR *dir = opendir(".");
    char buffer[RCVBUFSIZE];

    if (dir == NULL) {
        perror("opendir() failed");
        close(client_socket);
        return;
    }

    while ((dir_entry = readdir(dir)) != NULL) {
        snprintf(buffer, sizeof(buffer), "%s\n", dir_entry->d_name);
        send(client_socket, buffer, strlen(buffer), 0);
    }

    closedir(dir);
    close(client_socket);
}

void handle_client_diff(int client_socket) {
    struct dirent *dir_entry;
    DIR *dir = opendir(".");
    char buffer[RCVBUFSIZE];
    char server_files[RCVBUFSIZE * 10]; // Assuming max 10 files for simplicity
    int count = 0;

    if (dir == NULL) {
        perror("opendir() failed");
        close(client_socket);
        return;
    }

    // Collect server files
    while ((dir_entry = readdir(dir)) != NULL) {
        // Skip "." and ".."
        if (strcmp(dir_entry->d_name, ".") != 0 && strcmp(dir_entry->d_name, "..") != 0) {
            snprintf(buffer, sizeof(buffer), "%s\n", dir_entry->d_name);
            strcat(server_files, buffer);
            count++;
        }
    }

    closedir(dir);
    
    // Send server files list to client
    send(client_socket, server_files, strlen(server_files), 0);
    close(client_socket);
}

// Function to handle PULL request by sending the requested file
void handle_client_pull(int client_socket, const char* filename) {
    FILE *fp = fopen(filename, "rb");
    char buffer[RCVBUFSIZE];
    size_t bytes_read;

    if (fp == NULL) {
        const char *error_msg = "Error: File not found.\n";
        send(client_socket, error_msg, strlen(error_msg), 0);
        close(client_socket);
        return;
    }

    printf("Sending file '%s' to client...\n", filename);
    while ((bytes_read = fread(buffer, 1, sizeof(buffer), fp)) > 0) {
        send(client_socket, buffer, bytes_read, 0);
    }
    fclose(fp);
    printf("File '%s' sent successfully.\n", filename);
    close(client_socket);
}

// Function to handle LEAVE request
void handle_client_leave(int client_socket) {
    printf("Client has requested to leave. Closing connection...\n");
    close(client_socket);
}

// Function to handle a single client connection
void* handle_client(void* client_sock_ptr) {
    int client_socket = *(int*)client_sock_ptr;
    free(client_sock_ptr);  // Free the pointer passed to the thread
    Message msg;

    // Receive the message header
    if (recv(client_socket, &msg, sizeof(msg.header), 0) <= 0) {
        perror("recv() failed");
        close(client_socket);
        return NULL;
    }

    // If the message is a PULL request, receive additional data
    if (msg.header.type == PULL) {
        if (recv(client_socket, msg.data, msg.header.data_length, 0) <= 0) {
            perror("recv() failed");
            close(client_socket);
            return NULL;
        }
    }

    // Handle the message based on its type
    switch (msg.header.type) {
        case LIST:
            handle_client_list(client_socket);
            break;
        case DIFF:
            handle_client_diff(client_socket);
            break;
        case PULL:
            handle_client_pull(client_socket, msg.data);
            break;
        case LEAVE:
            handle_client_leave(client_socket);
            break;
        default:
            printf("Unknown message type received.\n");
            close(client_socket);
    }

    return NULL;
}

int main() {
    int serverSock;
    struct sockaddr_in serv_addr, clnt_addr;
    unsigned int clntLen;

    if ((serverSock = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0) {
        perror("socket() failed");
        exit(1);
    }

    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    serv_addr.sin_port = htons(9091);

    if (bind(serverSock, (struct sockaddr *) &serv_addr, sizeof(serv_addr)) < 0) {
        perror("bind() failed");
        exit(1);
    }

    if (listen(serverSock, 10) < 0) {
        perror("listen() failed");
        exit(1);
    }

    printf("Server is listening on port 9091...\n");

    while (1) {
        clntLen = sizeof(clnt_addr);
        int* clientSockPtr = malloc(sizeof(int));  // Allocate memory for the client socket
        if ((*clientSockPtr = accept(serverSock, (struct sockaddr *) &clnt_addr, &clntLen)) < 0) {
            perror("accept() failed");
            free(clientSockPtr);  // Free the memory if accept failed
            continue;
        }

        // Create a thread to handle the client
        pthread_t thread_id;
        if (pthread_create(&thread_id, NULL, handle_client, clientSockPtr) != 0) {
            perror("pthread_create() failed");
            close(*clientSockPtr);
            free(clientSockPtr);
        }

        // Detach the thread so resources are automatically freed when it terminates
        pthread_detach(thread_id);
    }

    close(serverSock);
    return 0;
}
