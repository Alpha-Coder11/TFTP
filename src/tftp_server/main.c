#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/select.h>

#define PORT 69
#define BUFFER_SIZE 516
#define BLOCK_SIZE 512
#define TIMEOUT 3

// TFTP Opcodes
#define RRQ 1
#define DATA 3
#define ACK 4
#define ERROR 5

// Function to send an error message

void send_error(int sockfd, struct sockaddr_in * client_addr, socklen_t len, const char * message) 
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

// Function to handle read requests
void handle_rrq(int sockfd, struct sockaddr_in * client_addr, socklen_t len, const char * filename) 
{
    FILE * fp = fopen(filename, "r");
    if (fp == NULL) 
    {
      send_error(sockfd, client_addr, len, "File not found ");
      return;
    }
    int block = 1, n;
    char buffer[BUFFER_SIZE];
    struct timeval tv;
    fd_set read_fds;
    
    // Send the file contents in blocks
    while ( (n = fread(buffer + 4, 1, BLOCK_SIZE, fp)) > 0 ) 
    {
        buffer[0] = 0;
        buffer[1] = DATA;
        buffer[2] = (block >> 8) & 0xFF;
        buffer[3] = block & 0xFF;
        int attempts = 0;
        while (attempts < 5) 
        {
            sendto(sockfd, buffer, n + 4, 0, (struct sockaddr * ) client_addr, len);
            // Wait for acknowledgment
            FD_ZERO( & read_fds);
            FD_SET(sockfd, & read_fds);
            tv.tv_sec = TIMEOUT;
            tv.tv_usec = 0;
            int s = select(sockfd + 1, & read_fds, NULL, NULL, & tv);
            if (s > 0) 
            {
                char ack_buffer[BUFFER_SIZE];
                recvfrom(sockfd, ack_buffer,
                BUFFER_SIZE, 0, (struct sockaddr * ) client_addr, &len);
                if (ack_buffer[1] == ACK && (ack_buffer[2] << 8 | ack_buffer[3]) == block) 
                {
                  break; // Correct ACK received
                }
            } 
            else 
            {
                printf("Timeout, retransmitting block %d\n ", block);
                attempts++;
            }
        }
        if (attempts == 5) 
        {
            printf("Failed to receive ACK for block %d, giving up\n ", block);
            fclose(fp);
            return;
        }
        block++;
    }
    fclose(fp);
}

void handle_wrq(int sockfd, struct sockaddr_in *client_addr, socklen_t len, const char* filename) 
{
    FILE *fp = fopen(filename, "w");
    if (fp == NULL) {perror("Failed to open file for writing");
    exit(EXIT_FAILURE);
    }
    int block = 0;
    char buffer[BUFFER_SIZE];
    // Send ACK for block 0
    send_ack(sockfd, client_addr, len, block);

    do 
    {
        // Receive data block
        int n = recvfrom(sockfd, buffer, BUFFER_SIZE,
        0, (struct sockaddr *) client_addr, &len);
        if (n < 0) 
        {
            perror("recvfrom failed");
            fclose(fp);
            return;
        }
        if (buffer[1] == DATA && (buffer[2] << 8 | buffer[3]) == block + 1) 
        {
            fwrite(buffer + 4, 1, n - 4, fp); // Write data to file
            block++;
            send_ack(sockfd, client_addr, len, block); // Send ACK for this block
        }
    }
    while (buffer[1] == DATA && (n - 4) == BLOCK_SIZE);

    fclose(fp);
}

int main() 
{
    int sockfd;
    struct sockaddr_in servaddr, cliaddr;
    socklen_t len;
    char buffer[BUFFER_SIZE];
    // Create socket file descriptor
    if ((sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0 ) 
    {
      perror("socket creation failed");
      exit(EXIT_FAILURE);
    }
    memset( & servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = INADDR_ANY;
    servaddr.sin_port = htons(PORT);

    // Bind the socket with the server address
    if (bind(sockfd, (const struct sockaddr *) & servaddr, sizeof(servaddr)) < 0) 
    {
      perror("bind failed");
      exit(EXIT_FAILURE);
    }

    while (1) 
    {
        printf("Waiting for RRQ...\n");
        len = sizeof(cliaddr);
        // Receive RRQ from client
        int n = recvfrom(sockfd, buffer, BUFFER_SIZE, 0, (struct sockaddr * ) & cliaddr, & len);
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
            handle_rrq(sockfd, & cliaddr, len, filename);
        }
        else if (buffer[1] == WRQ)
        {
            char* filename = buffer + 2;
            printf("WRQ received for file: %s\n", filename);
            handle_wrq(sockfd, &cliaddr, len, filename);
        }
    }
    // Close the socket
    close(sockfd);
    return 0;
}