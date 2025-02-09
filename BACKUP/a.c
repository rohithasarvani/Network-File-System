// file_transfer.h
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <errno.h>

#define BUFFER_SIZE 1024

// Structure for file transfer
struct FileTransferPacket {
    char filename[256];
    char data[BUFFER_SIZE];
    size_t size;
    int isLast;
};

// server.c
void start_server(int port) {
    int server_fd, client_fd;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len = sizeof(client_addr);
    
    // Create socket
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }
    
    // Configure server address
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);
    
    // Bind socket
    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Bind failed");
        exit(EXIT_FAILURE);
    }
    
    // Listen for connections
    if (listen(server_fd, 1) < 0) {
        perror("Listen failed");
        exit(EXIT_FAILURE);
    }
    
    printf("Server listening on port %d...\n", port);
    
    while (1) {
        client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_len);
        if (client_fd < 0) {
            perror("Accept failed");
            continue;
        }
        
        printf("Client connected from %s:%d\n", 
               inet_ntoa(client_addr.sin_addr), 
               ntohs(client_addr.sin_port));
        
        // Receive file
        struct FileTransferPacket packet;
        FILE *fp = NULL;
        
        // Receive initial packet with filename
        recv(client_fd, &packet, sizeof(packet), 0);
        printf("Receiving file: %s\n", packet.filename);
        
        fp = fopen(packet.filename, "wb");
        if (!fp) {
            perror("File creation failed");
            close(client_fd);
            continue;
        }
        
        // Receive file data
        do {
            recv(client_fd, &packet, sizeof(packet), 0);
            fwrite(packet.data, 1, packet.size, fp);
        } while (!packet.isLast);
        
        fclose(fp);
        printf("File received successfully\n");
        close(client_fd);
    }
    
    close(server_fd);
}

// client.c
void send_file(const char *filepath, const char *dest_ip, int dest_port) {
    int sock_fd;
    struct sockaddr_in server_addr;
    struct FileTransferPacket packet;
    FILE *fp;
    
    // Create socket
    sock_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (sock_fd == -1) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }
    
    // Configure server address
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(dest_port);
    
    if (inet_pton(AF_INET, dest_ip, &server_addr.sin_addr) <= 0) {
        perror("Invalid address");
        exit(EXIT_FAILURE);
    }
    
    // Connect to server
    if (connect(sock_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Connection failed");
        exit(EXIT_FAILURE);
    }
    
    // Open file
    fp = fopen(filepath, "rb");
    if (!fp) {
        perror("File opening failed");
        close(sock_fd);
        exit(EXIT_FAILURE);
    }
    
    // Get filename from path
    const char *filename = strrchr(filepath, '/');
    filename = filename ? filename + 1 : filepath;
    
    // Send filename first
    memset(&packet, 0, sizeof(packet));
    strncpy(packet.filename, filename, sizeof(packet.filename) - 1);
    send(sock_fd, &packet, sizeof(packet), 0);
    
    // Send file data
    while (!feof(fp)) {
        memset(&packet, 0, sizeof(packet));
        packet.size = fread(packet.data, 1, BUFFER_SIZE, fp);
        packet.isLast = feof(fp) ? 1 : 0;
        
        if (send(sock_fd, &packet, sizeof(packet), 0) < 0) {
            perror("Send failed");
            fclose(fp);
            close(sock_fd);
            exit(EXIT_FAILURE);
        }
    }
    
    printf("File sent successfully\n");
    fclose(fp);
    close(sock_fd);
}

// main.c - Example usage
int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Usage:\n");
        printf("Server mode: %s -s <port>\n", argv[0]);
        printf("Client mode: %s -c <filepath> <destination_ip> <destination_port>\n", argv[0]);
        return 1;
    }
    
    if (strcmp(argv[1], "-s") == 0) {
        if (argc != 3) {
            printf("Server mode usage: %s -s <port>\n", argv[0]);
            return 1;
        }
        start_server(atoi(argv[2]));
    } else if (strcmp(argv[1], "-c") == 0) {
        if (argc != 5) {
            printf("Client mode usage: %s -c <filepath> <destination_ip> <destination_port>\n", argv[0]);
            return 1;
        }
        send_file(argv[2], argv[3], atoi(argv[4]));
    } else {
        printf("Invalid mode. Use -s for server or -c for client.\n");
        return 1;
    }
    
    return 0;
}