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
#define MAX_NAME_LEN 512

// Define message types
typedef enum
{
    LIST = 1,
    DIFF = 2,
    PULL = 3,
    LEAVE = 4
} MessageType;

// Define the message header structure
typedef struct
{
    MessageType type;
    uint32_t data_length;
} MessageHeader;

// Define the complete message structure
typedef struct
{
    MessageHeader header;
    char data[256];
} Message;

char client_dir[] = "./client"; // Directory where client stores music files

// Function to handle LIST request
void send_list_request(int sockfd)
{
    Message list_msg;
    list_msg.header.type = LIST;
    list_msg.header.data_length = 0;

    send(sockfd, &list_msg, sizeof(list_msg.header), 0);

    char buffer[RCVBUFSIZE];
    int received;
    printf("Files on the server:\n");
    while ((received = recv(sockfd, buffer, RCVBUFSIZE - 1, 0)) > 0)
    {
        buffer[received] = '\0';
        printf("%s", buffer);
        if (received < RCVBUFSIZE - 1)
            break;
    }
    printf("\n");
}

void compute_file_hash(const char *file_path, char *hash_str)
{
    unsigned char hash[MD5_DIGEST_LENGTH];
    FILE *file = fopen(file_path, "rb");
    if (!file)
    {
        strcpy(hash_str, "error");
        return;
    }

    MD5_CTX md5_ctx;
    MD5_Init(&md5_ctx);
    char buffer[1024];
    size_t bytes;
    while ((bytes = fread(buffer, 1, sizeof(buffer), file)) != 0)
    {
        MD5_Update(&md5_ctx, buffer, bytes);
    }
    MD5_Final(hash, &md5_ctx);
    fclose(file);

    for (int i = 0; i < MD5_DIGEST_LENGTH; i++)
    {
        sprintf(&hash_str[i * 2], "%02x", hash[i]);
    }
    hash_str[MD5_DIGEST_LENGTH * 2] = '\0'; // Null-terminate the hash string
}

void send_diff_request(int sockfd, const char *client_dir, char missingFiles[][MAX_NAME_LEN], int *count)
{
    Message diff_msg;
    diff_msg.header.type = DIFF;
    diff_msg.header.data_length = 0;
    // reset our missing count first
    *count = 0;

    // Send the DIFF request to the server
    send(sockfd, &diff_msg, sizeof(diff_msg.header), 0);

    // Get the client's files and hashes
    char client_file_hashes[RCVBUFSIZE * 10] = "";
    DIR *dir = opendir(client_dir);
    if (!dir)
    {
        perror("opendir() failed");
        return;
    }

    struct dirent *dir_entry;
    while ((dir_entry = readdir(dir)) != NULL)
    {
        if (strcmp(dir_entry->d_name, ".") != 0 && strcmp(dir_entry->d_name, "..") != 0)
        {
            char file_path[512];
            snprintf(file_path, sizeof(file_path), "%s/%s", client_dir, dir_entry->d_name);

            char hash_str[MD5_DIGEST_LENGTH * 2 + 1];
            compute_file_hash(file_path, hash_str);

            snprintf(client_file_hashes + strlen(client_file_hashes), sizeof(client_file_hashes) - strlen(client_file_hashes),
                     "%s %s\n", dir_entry->d_name, hash_str);
        }
    }
    closedir(dir);

    // Print the client's file list and hashes
    // printf("Client files and hashes:\n%s\n", client_file_hashes);

    // Receive server's file list and hashes
    char server_files[RCVBUFSIZE * 10] = "";
    int received = recv(sockfd, server_files, sizeof(server_files) - 1, 0);
    server_files[received] = '\0';

    // Print the server's file list and hashes
    // printf("Server files and hashes:\n%s\n", server_files);

    // Compare files and hashes
    printf("Files missing or different on client:\n");

    char server_filename[256], server_hash[MD5_DIGEST_LENGTH * 2 + 1];
    char *server_ptr = server_files;

    while (sscanf(server_ptr, "%255s %32s", server_filename, server_hash) == 2)
    {
        // Move to the next line in the server files list
        server_ptr = strchr(server_ptr, '\n');
        if (server_ptr)
            server_ptr++; // skip '\n'

        // Check if the server file's hash exists in the client's hashes
        int hash_found = 0;
        char client_filename[256], client_hash[MD5_DIGEST_LENGTH * 2 + 1];
        char *client_ptr = client_file_hashes;

        while (sscanf(client_ptr, "%255s %32s", client_filename, client_hash) == 2)
        {
            // Move to the next line in the client files list
            client_ptr = strchr(client_ptr, '\n');
            if (client_ptr)
                client_ptr++; // skip '\n'

            // Compare the server file's hash with the client's
            if (strcmp(client_hash, server_hash) == 0)
            {
                hash_found = 1; // Found a matching file and hash
                break;
            }
        }

        // Print only if no matching hash was found
        if (!hash_found)
        {
            printf("%s\n", server_filename);

            // now copy filename to our missing files array
            strcpy(missingFiles[*count], server_filename);
            (*count)++;
        }
    }
}

void send_pull_request(int sockfd, char missingFiles[][MAX_NAME_LEN], int missingCount)
{
    for (int i = 0; i < missingCount; i++)
    {
        Message pull_msg;
        pull_msg.header.type = PULL;
        pull_msg.header.data_length = strlen(missingFiles[i]);
        strcpy(pull_msg.data, missingFiles[i]);

        // Send the PULL request to the server
        if (send(sockfd, &pull_msg, sizeof(pull_msg.header) + pull_msg.header.data_length, 0) < 0)
        {
            perror("Error sending PULL request");
            close(sockfd);
            return;
        }

        long file_size;
        recv(sockfd, &file_size, sizeof(file_size), 0); // Receive file size

        char *buffer = (char *)malloc(file_size);
        if (buffer == NULL)
        {
            perror("Error allocating memory for file content");
            return;
        }

        size_t total_received = 0;
        while (total_received < file_size)
        {
            ssize_t bytes_received = recv(sockfd, buffer + total_received, file_size - total_received, 0);
            if (bytes_received < 0)
            {
                perror("Error receiving file data");
                free(buffer);
                return; // Exit on error
            }
            total_received += bytes_received;
        }

        // Create a valid path for saving the file
        char file_path[512];
        snprintf(file_path, sizeof(file_path), "%s/%s", client_dir, missingFiles[i]); // Save to client directory

        // Write to file
        FILE *fp = fopen(file_path, "wb");
        if (fp == NULL)
        {
            perror("Error opening file for writing");
            free(buffer);
            return;
        }

        // write to our client folder, close our file, and free our buffer
        fwrite(buffer, 1, file_size, fp);
        fclose(fp);
        free(buffer);

        printf("File '%s' downloaded successfully to '%s'.\n", missingFiles[i], file_path);
    }
}

// Function to handle LEAVE request
void send_leave_request(int sockfd)
{
    Message leave_msg;
    leave_msg.header.type = LEAVE;
    leave_msg.header.data_length = 0;

    send(sockfd, &leave_msg, sizeof(leave_msg.header), 0);
    printf("Client sent LEAVE message and is disconnecting...\n");
    close(sockfd);
}

int main(int argc, char *argv[])
{
    int clientSock;
    char missingFiles[256][MAX_NAME_LEN];
    int missingCount = 0;
    struct sockaddr_in serv_addr;

    if ((clientSock = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP)) < 0)
    {
        perror("socket() failed");
        exit(1);
    }

    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    serv_addr.sin_port = htons(8094);

    if (connect(clientSock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
    {
        perror("connect() failed");
        exit(1);
    }

    int client_left = 0;
    while (client_left == 0)
    {
        char input;
        char filename[256];
        printf("Enter '1' for LIST, '2' for DIFF, '3' for PULL, or '4' for LEAVE: ");
        scanf(" %c", &input);

        if (input == '1')
        {
            send_list_request(clientSock);
        }
        else if (input == '2')
        {
            send_diff_request(clientSock, client_dir, missingFiles, &missingCount);
        }
        else if (input == '3')
        {
            if (missingCount > 0)
                send_pull_request(clientSock, missingFiles, missingCount);
            else
                printf("No missing files to pull, run DIFF first.\n");
        }
        else if (input == '4')
        {
            send_leave_request(clientSock);
            client_left = 1; // Correct assignment for exit
        }
        else
        {
            printf("Invalid input.\n");
        }
    }

    return 0;
}
