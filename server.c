#include <stdio.h>       /* for printf() and fprintf() */
#include <sys/socket.h>  /* for socket(), connect(), send(), and recv() */
#include <arpa/inet.h>   /* for sockaddr_in and inet_addr() */
#include <stdlib.h>      /* supports all sorts of functionality */
#include <unistd.h>      /* for close() */
#include <string.h>      /* support any string ops */
#include <openssl/evp.h> /* for OpenSSL EVP digest libraries/SHA256 */

#define RCVBUFSIZE 512 /* The receive buffer size */
#define SNDBUFSIZE 512 /* The send buffer size */
#define BUFSIZE 40     /* Your name can be as many as 40 chars*/
#define MAXPENDING 10

/* The main function */
int main(int argc, char *argv[])
{

    // unsigned short servPort = 9090;
    int serverSock;                       /* Server Socket */
    int clientSock;                       /* Client Socket */
    struct sockaddr_in changeServAddr;    /* Local address */
    struct sockaddr_in changeClntAddr;    /* Client address */
    unsigned short changeServPort = 9090; /* Server port */
    unsigned int clntLen;                 /* Length of address data struct */

    char nameBuf[BUFSIZE];                   /* Buff to store name from client */
    unsigned char md_value[EVP_MAX_MD_SIZE]; /* Buff to store change result */
    EVP_MD_CTX *mdctx;                       /* Digest data structure declaration */
    const EVP_MD *md;                        /* Digest data structure declaration */
    int md_len;                              /* Digest data structure size tracking */

    /* Create new TCP Socket for incoming requests*/
    serverSock = socket(PF_INET, SOCK_STREAM, 0);
    if (serverSock < 0)
    {
        printf("Error, socket() failed");
        exit(1);
    }

    /* Construct local address structure*/
    memset(&changeServAddr, 0, sizeof(changeServAddr));
    changeServAddr.sin_family = AF_INET;
    changeServAddr.sin_addr.s_addr = htonl(INADDR_ANY);
    changeServAddr.sin_port = htons(changeServPort);

    /* Bind to local address structure */
    if (bind(serverSock, (struct sockaddr *)&changeServAddr, sizeof(changeServAddr)) < 0)
    {
        printf("Error, bind() failed\n");
        exit(1);
    }

    /* Listen for incoming connections */
    if (listen(serverSock, MAXPENDING) < 0)
    {
        printf("Error, listen() failed\n");
        exit(1);
    }

    /* Loop server forever*/
    while (1)
    {
        /* Accept incoming connection */
        clntLen = sizeof(changeClntAddr);
        clientSock = accept(serverSock, (struct sockaddr *)&changeClntAddr, &clntLen);

        /* Extract Your Name from the packet, store in nameBuf */
        if (recv(clientSock, nameBuf, BUFSIZE, 0) < 0)
        {
            printf("Error, recv() failed\n");
            exit(1);
        }

        /* Run this and return the final value in md_value to client */
        /* Takes the client name and changes it */
        /* Students should NOT touch this code */
        OpenSSL_add_all_digests();
        md = EVP_get_digestbyname("SHA256");
        mdctx = EVP_MD_CTX_create();
        EVP_DigestInit_ex(mdctx, md, NULL);
        EVP_DigestUpdate(mdctx, nameBuf, strlen(nameBuf));
        EVP_DigestFinal_ex(mdctx, md_value, &md_len);
        EVP_MD_CTX_destroy(mdctx);

        /* Return md_value to client */
        if (send(clientSock, md_value, md_len, 0) < 0)
        {
            printf("Error, send() failed\n");
            exit(1);
        }
        close(clientSock);
    }
    close(serverSock);
}
