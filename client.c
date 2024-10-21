#include <stdio.h>      /* for printf() and fprintf() */
#include <sys/socket.h> /* for socket(), connect(), send(), and recv() */
#include <arpa/inet.h>  /* for sockaddr_in and inet_addr() */
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <openssl/evp.h> /* for OpenSSL EVP digest libraries/SHA256 */

/* Constants */
#define RCVBUFSIZE 512 /* The receive buffer size */
#define SNDBUFSIZE 512 /* The send buffer size */
#define MDLEN 32

/* The main function */
int main(int argc, char *argv[])
{

    int clientSock;               /* socket descriptor */
    struct sockaddr_in serv_addr; /* The server address */
    unsigned short servPort = 8080;
    unsigned int num_bytes; /* number of bytes sent or receiver in our socket */

    char *studentName; /* Your Name */

    char sndBuf[SNDBUFSIZE]; /* Send Buffer */
    char rcvBuf[RCVBUFSIZE]; /* Receive Buffer */

    int i; /* Counter Value */

    /* Get the Student Name from the command line */
    if (argc != 2)
    {
        printf("Incorrect input format. The correct format is:\n\tnameChanger your_name\n");
        exit(1);
    }
    studentName = argv[1];
    memset(&sndBuf, 0, RCVBUFSIZE);
    memset(&rcvBuf, 0, RCVBUFSIZE);

    /* Create a new TCP socket*/
    int sock = socket(PF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock < 0)
    {
        printf("Error, socket() failed\n");
        exit(1);
    }

    /* Construct the server address structure */
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    serv_addr.sin_port = htons(servPort);

    /* Establish connecction to the server */
    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0)
    {
        printf("Error, connect() failed\n");
        exit(1);
    }

    /* Send the string to the server */
    num_bytes = send(sock, sndBuf, SNDBUFSIZE, 0);

    /* Receive and print response from the server */
    num_bytes = recv(sock, rcvBuf, RCVBUFSIZE - 1, 0);
    rcvBuf[num_bytes] = '\0';

    printf("%s\n", studentName);
    printf("Transformed input is: ");
    for (i = 0; i < MDLEN; i++)
        printf("%02x", rcvBuf[i]);
    printf("\n");

    return 0;
}
