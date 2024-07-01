#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

#define PORT 69
#define BUFFER_SIZE 516
#define BLOCK_SIZE 512
// TFTP Opcodes
#define RRQ 1
#define DATA 3
#define ERROR 5

// Function to send an error message
void send_error(int sockfd, struct sockaddr_in *client_addr, socklen_t len, const char * message) 
{
    char buffer[BUFFER_SIZE];
    int message_len = strlen(message);
    // Prepare the error packet
    buffer[0] = 0;
    buffer[1] = ERROR;
    buffer[2] = 0;
    buffer[3] = 1; // Error code 1 (File not found)
    strcpy(buffer + 4, message);
    buffer[message_len + 4] = 0;
    sendto(sockfd, buffer, message_len + 5, 0, (struct sockaddr * ) client_addr, len);
}

// Main function to handle RRQ
int main() 
{
    int sockfd;
    struct sockaddr_in servaddr, cliaddr;
    socklen_t len;
    char buffer[BUFFER_SIZE];
    // Create socket file descriptor
    if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
    {
        perror("socket creation failed");
        exit(EXIT_FAILURE);
    }
    memset( & servaddr, 0, sizeof(servaddr));
    // Filling server information
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = INADDR_ANY;
    servaddr.sin_port = htons(PORT);
    // Bind the socket with the server address
    if (bind(sockfd, (const struct sockaddr *)&servaddr, sizeof(servaddr)) < 0) 
    {
        perror("bind failed");
        exit(EXIT_FAILURE);
    }
    while (1) 
    {
        printf("Waiting for RRQ...\n");
        len = sizeof(cliaddr);
        // Receive RRQ from client
        int n = recvfrom(sockfd, buffer, BUFFER_SIZE, 0, (struct sockaddr * ) &cliaddr, &len);
        if (n < 0) 
        {
            perror("recvfrom failed");
            continue;
        }
        // Check if it's an RRQ
        if (buffer[1] == RRQ) 
        {
            printf("RRQ received. Processing...\n");
            // Extract filename and mode
            char * filename = buffer + 2;
            char * mode = filename + strlen(filename) + 1; // Open the requested file
            FILE * fp = fopen(filename, "r");
            if (fp == NULL) 
            {
                send_error(sockfd,&cliaddr, len, "File not found");
                continue;
            }
            // Send the file contents in blocks
            int block = 1;
            while ((n = fread(buffer + 4, 1, BLOCK_SIZE, fp)) > 0) 
            {
                buffer[0] = 0;
                buffer[1] = DATA;
                buffer[2] = (block >> 8) & 0xFF;
                buffer[3] = block & 0xFF;
                sendto(sockfd, buffer, n+4, 0, (struct sockaddr * ) & cliaddr, len);
                block++;
            }
            
            fclose(fp);
        }
    }
    // Close the socket
    close(sockfd);
    return 0;
}
