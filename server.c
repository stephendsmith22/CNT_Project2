#include <stdio.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <dirent.h>
#include <pthread.h>
#include <openssl/md5.h>

#define RCVBUFSIZE 512

typedef enum
{
    LIST = 1,
    DIFF = 2,
    PULL = 3,
    LEAVE = 4
} MessageType;

typedef struct
{
    MessageType type;
    uint32_t data_length = 0;
} MessageHeader;

typedef struct
{
    MessageHeader header;
    char data[256];
} Message;

void handle_client_list(int client_socket)
{
    struct dirent *dir_entry;
    DIR *dir = opendir("./server");
    char buffer[RCVBUFSIZE];
    int total_sent = 0; // Track total bytes sent

    if (dir == NULL)
    {
        perror("opendir failed");
        return;
    }

    // Gather all filenames
    while ((dir_entry = readdir(dir)) != NULL)
    {
        if (strcmp(dir_entry->d_name, ".") != 0 && strcmp(dir_entry->d_name, "..") != 0)
        {
            int len = snprintf(buffer + total_sent, sizeof(buffer) - total_sent, "%s\n", dir_entry->d_name);
            if (len < 0 || total_sent + len >= sizeof(buffer))
            {
                break; // Prevent overflow
            }
            total_sent += len;
        }
    }

    // Send all filenames in one go
    send(client_socket, buffer, total_sent, 0);
    closedir(dir);
    printf("Sent file list to client.\n");
}

void handle_client_diff(int client_socket)
{
    // Prepare to gather server file hashes
    char server_file_hashes[RCVBUFSIZE * 10] = "";
    struct dirent *dir_entry;
    DIR *dir = opendir("./server");

    if (dir == NULL)
    {
        perror("opendir failed");
        return;
    }

    while ((dir_entry = readdir(dir)) != NULL)
    {
        if (strcmp(dir_entry->d_name, ".") != 0 && strcmp(dir_entry->d_name, "..") != 0)
        {
            // Generate file hash
            char file_path[512];
            snprintf(file_path, sizeof(file_path), "./server/%s", dir_entry->d_name);

            unsigned char hash[MD5_DIGEST_LENGTH];
            MD5_CTX md5_ctx;
            MD5_Init(&md5_ctx);
            FILE *file = fopen(file_path, "rb");
            if (file != NULL)
            {
                char file_buffer[1024];
                size_t bytes;
                while ((bytes = fread(file_buffer, 1, sizeof(file_buffer), file)) != 0)
                {
                    MD5_Update(&md5_ctx, file_buffer, bytes);
                }
                MD5_Final(hash, &md5_ctx);
                fclose(file);
            }

            char hash_str[MD5_DIGEST_LENGTH * 2 + 1];
            for (int i = 0; i < MD5_DIGEST_LENGTH; i++)
            {
                sprintf(&hash_str[i * 2], "%02x", hash[i]);
            }
            hash_str[MD5_DIGEST_LENGTH * 2] = '\0';

            snprintf(server_file_hashes + strlen(server_file_hashes), sizeof(server_file_hashes) - strlen(server_file_hashes),
                     "%s %s\n", dir_entry->d_name, hash_str);
        }
    }

    // Send the complete list of server file hashes to the client
    send(client_socket, server_file_hashes, strlen(server_file_hashes), 0);
    closedir(dir);
    printf("Sent DIFF information to client.\n");
}

// Function to handle PULL request
void handle_client_pull(int client_socket, const char *filename)
{
    char file_path[512];
    snprintf(file_path, sizeof(file_path), "./server/%s", filename);

    FILE *file = fopen(file_path, "rb");
    if (file == NULL)
    {
        // File not found, send error message to client
        char error_msg[] = "Error: File not found.\n";
        send(client_socket, error_msg, strlen(error_msg), 0);
        printf("Error: %s\n", error_msg);
        return;
    }

    // Get file size
    fseek(file, 0, SEEK_END);
    long file_size = ftell(file);
    fseek(file, 0, SEEK_SET); // Reset to beginning of file

    // Send the file size to the client first
    if (send(client_socket, &file_size, sizeof(file_size), 0) < 0)
    {
        perror("Failed to send file size");
        fclose(file);
        return;
    }

    // buffer and hold file chunks
    char buffer[RCVBUFSIZE];
    while (1)
    {
        size_t bytes_read = fread(buffer, 1, sizeof(buffer), file);
        if (bytes_read == 0)
        {
            // check for end of file, or an error
            if (feof(file))
                printf("Reached end of file '%s'.\n", filename);
            else if (ferror(file))
                perror("Error reading file");
            break;
        }

        // now we send the chunk to the client
        size_t total_sent = 0;
        while (total_sent < bytes_read)
        {
            ssize_t bytes_sent = send(client_socket, buffer + total_sent, bytes_read - total_sent, 0);
            if (bytes_sent < 0)
            {
                perror("Error sending file data");
                fclose(file);
                return;
            }
            total_sent += bytes_sent;
        }
    }
    fclose(file);
    printf("Sent file '%s' to client.\n", filename);
}

// Function to handle LEAVE request
void handle_client_leave(int client_socket)
{
    close(client_socket);
    printf("Client disconnected.\n");
}

// Client handler function
void *client_handler(void *socket_desc)
{
    int client_socket = *(int *)socket_desc;
    Message msg;

    while (recv(client_socket, &msg.header, sizeof(msg.header), 0) > 0)
    {
        switch (msg.header.type)
        {
        case LIST:
            handle_client_list(client_socket);
            break;
        case DIFF:
            handle_client_diff(client_socket);
            break;
        case PULL:
            recv(client_socket, msg.data, msg.header.data_length, 0);
            handle_client_pull(client_socket, msg.data);
            break;
        case LEAVE:
            handle_client_leave(client_socket);
            return 0;
        default:
            printf("Unknown request type received.\n");
            break;
        }
    }

    handle_client_leave(client_socket);
    return 0;
}

int main(int argc, char *argv[])
{
    int server_sock, client_sock;
    struct sockaddr_in server, client;

    // Create socket
    server_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (server_sock == -1)
    {
        perror("Could not create socket");
        return 1;
    }

    // Prepare the sockaddr_in structure
    server.sin_family = AF_INET;
    server.sin_addr.s_addr = INADDR_ANY;
    server.sin_port = htons(8092);

    // Bind
    if (bind(server_sock, (struct sockaddr *)&server, sizeof(server)) < 0)
    {
        perror("bind failed");
        return 1;
    }

    // Listen
    listen(server_sock, 3);
    printf("Waiting for incoming connections...\n");
    socklen_t client_size = sizeof(client);

    // Accept incoming connection
    while ((client_sock = accept(server_sock, (struct sockaddr *)&client, &client_size)))
    {
        printf("Connection accepted\n");

        pthread_t client_thread;
        if (pthread_create(&client_thread, NULL, client_handler, (void *)&client_sock) < 0)
        {
            perror("Could not create thread");
            return 1;
        }
    }

    if (client_sock < 0)
    {
        perror("accept failed");
        return 1;
    }

    close(server_sock);
    return 0;
}
