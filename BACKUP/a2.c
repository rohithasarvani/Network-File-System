#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <dirent.h>
#include <errno.h>
#include <libgen.h>

#define BUFFER_SIZE 4096
#define PATH_MAX 4096

// Packet types
#define PACKET_DIR_START 1       // Start of directory structure transfer
#define PACKET_DIR_CREATE 2      // Directory creation command
#define PACKET_DIR_END 3         // End of directory structure transfer
#define PACKET_FILE_START 4      // Start of file transfer
#define PACKET_FILE_DATA 5       // File data
#define PACKET_FILE_END 6        // End of file transfer
#define PACKET_TRANSFER_DONE 7   // Complete transfer done

// Structure for transfer packets
struct TransferPacket {
    int type;                    // Packet type
    char path[PATH_MAX];         // Relative path
    char data[BUFFER_SIZE];      // File data
    size_t size;                 // Data size
    mode_t mode;                 // File/directory permissions
    char base_dir[PATH_MAX];     // Base directory name
};

// Function to create directory recursively
void create_directory_recursive(const char *path, mode_t mode) {
    char tmp[PATH_MAX];
    char *p = NULL;
    size_t len;
    
    snprintf(tmp, sizeof(tmp), "%s", path);
    len = strlen(tmp);
    if (tmp[len - 1] == '/')
        tmp[len - 1] = 0;
    
    for (p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = 0;
            mkdir(tmp, mode);
            *p = '/';
        }
    }
    mkdir(tmp, mode);
}

// Get relative path
char* get_relative_path(const char *base_path, const char *full_path) {
    static char relative[PATH_MAX];
    size_t base_len = strlen(base_path);
    
    // Handle the case where base_path is the immediate parent
    if (strncmp(base_path, full_path, base_len) == 0) {
        if (full_path[base_len] == '/')
            strcpy(relative, full_path + base_len + 1);
        else
            strcpy(relative, full_path + base_len);
        return relative;
    }
    
    // Extract just the last directory name from base_path
    char *base_name = basename(strdup(base_path));
    sprintf(relative, "%s/%s", base_name, full_path + base_len + strlen(base_name) + 2);
    return relative;
}

// Function to scan and create directory structure
void scan_directory_structure(int sock_fd, const char *base_path, const char *dir_path) {
    DIR *dir;
    struct dirent *entry;
    struct stat path_stat;
    char full_path[PATH_MAX];
    struct TransferPacket packet;
    
    dir = opendir(dir_path);
    if (!dir) {
        perror("Failed to open directory");
        return;
    }
    
    // Send directory creation packet
    memset(&packet, 0, sizeof(packet));
    packet.type = PACKET_DIR_CREATE;
    packet.mode = S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH;  // Default directory permissions
    strncpy(packet.base_dir, basename(strdup(base_path)), PATH_MAX - 1);
    strncpy(packet.path, get_relative_path(base_path, dir_path), PATH_MAX - 1);
    send(sock_fd, &packet, sizeof(packet), 0);
    
    while ((entry = readdir(dir))) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;
            
        snprintf(full_path, sizeof(full_path), "%s/%s", dir_path, entry->d_name);
        if (stat(full_path, &path_stat) == -1)
            continue;
            
        if (S_ISDIR(path_stat.st_mode)) {
            scan_directory_structure(sock_fd, base_path, full_path);
        }
    }
    
    closedir(dir);
}

// Function to send directory contents after structure is created
void send_directory_contents(int sock_fd, const char *base_path, const char *dir_path) {
    DIR *dir;
    struct dirent *entry;
    struct stat path_stat;
    char full_path[PATH_MAX];
    struct TransferPacket packet;
    
    dir = opendir(dir_path);
    if (!dir) {
        perror("Failed to open directory");
        return;
    }
    
    while ((entry = readdir(dir))) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;
            
        snprintf(full_path, sizeof(full_path), "%s/%s", dir_path, entry->d_name);
        if (stat(full_path, &path_stat) == -1)
            continue;
            
        if (S_ISDIR(path_stat.st_mode)) {
            send_directory_contents(sock_fd, base_path, full_path);
        } else if (S_ISREG(path_stat.st_mode)) {
            FILE *fp = fopen(full_path, "rb");
            if (!fp) continue;
            
            // Send file start packet
            memset(&packet, 0, sizeof(packet));
            packet.type = PACKET_FILE_START;
            packet.mode = path_stat.st_mode;
            strncpy(packet.base_dir, basename(strdup(base_path)), PATH_MAX - 1);
            strncpy(packet.path, get_relative_path(base_path, full_path), PATH_MAX - 1);
            send(sock_fd, &packet, sizeof(packet), 0);
            
            // Send file data
            while (!feof(fp)) {
                memset(&packet.data, 0, BUFFER_SIZE);
                packet.type = PACKET_FILE_DATA;
                packet.size = fread(packet.data, 1, BUFFER_SIZE, fp);
                send(sock_fd, &packet, sizeof(packet), 0);
            }
            
            // Send file end packet
            packet.type = PACKET_FILE_END;
            send(sock_fd, &packet, sizeof(packet), 0);
            
            fclose(fp);
            printf("Sent file: %s\n", full_path);
        }
    }
    
    closedir(dir);
}

// Server implementation
void start_server(int port) {
    int server_fd, client_fd;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len = sizeof(client_addr);
    char current_base_dir[PATH_MAX] = {0};
    
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }
    
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);
    
    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        perror("Bind failed");
        exit(EXIT_FAILURE);
    }
    
    if (listen(server_fd, 1) < 0) {
        perror("Listen failed");
        exit(EXIT_FAILURE);
    }
    
    printf("Server listening on port %d...\n", port);
    
    while (1) {
        FILE *fp = NULL;
        struct TransferPacket packet;
        
        client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_len);
        if (client_fd < 0) {
            perror("Accept failed");
            continue;
        }
        
        printf("Client connected from %s:%d\n", 
               inet_ntoa(client_addr.sin_addr), 
               ntohs(client_addr.sin_port));
        
        while (1) {
            ssize_t bytes_received = recv(client_fd, &packet, sizeof(packet), 0);
            if (bytes_received <= 0) break;
            
            // Update base directory if it changes
            if (strlen(packet.base_dir) > 0 && strcmp(current_base_dir, packet.base_dir) != 0) {
                strcpy(current_base_dir, packet.base_dir);
                create_directory_recursive(current_base_dir, S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
            }
            
            char full_path[PATH_MAX];
            snprintf(full_path, sizeof(full_path), "%s/%s", current_base_dir, packet.path);
            
            switch (packet.type) {
                case PACKET_DIR_CREATE:
                    printf("Creating directory: %s\n", full_path);
                    create_directory_recursive(full_path, packet.mode);
                    break;
                    
                case PACKET_FILE_START:
                    printf("Receiving file: %s\n", full_path);
                    if (fp) fclose(fp);
                    create_directory_recursive(dirname(strdup(full_path)), S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH);
                    fp = fopen(full_path, "wb");
                    if (!fp) {
                        perror("File creation failed");
                        continue;
                    }
                    chmod(full_path, packet.mode);
                    break;
                    
                case PACKET_FILE_DATA:
                    if (fp) {
                        fwrite(packet.data, 1, packet.size, fp);
                    }
                    break;
                    
                case PACKET_FILE_END:
                    if (fp) {
                        fclose(fp);
                        fp = NULL;
                        printf("File completed: %s\n", full_path);
                    }
                    break;
                    
                case PACKET_TRANSFER_DONE:
                    printf("Transfer completed\n");
                    goto transfer_done;
            }
        }
        
    transfer_done:
        if (fp) fclose(fp);
        close(client_fd);
    }
    
    close(server_fd);
}

// Client implementation
void send_folder(const char *folder_path, const char *dest_ip, int dest_port) {
    int sock_fd;
    struct sockaddr_in server_addr;
    struct TransferPacket packet;
    
    // Validate folder
    DIR *dir = opendir(folder_path);
    if (!dir) {
        perror("Invalid folder path");
        exit(EXIT_FAILURE);
    }
    closedir(dir);
    
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
    
    printf("Connected to server. Starting folder transfer...\n");
    
    // First, send directory structure
    printf("Creating directory structure...\n");
    scan_directory_structure(sock_fd, folder_path, folder_path);
    
    // Then, send all files
    printf("Transferring files...\n");
    send_directory_contents(sock_fd, folder_path, folder_path);
    
    // Send transfer completion packet
    memset(&packet, 0, sizeof(packet));
    packet.type = PACKET_TRANSFER_DONE;
    send(sock_fd, &packet, sizeof(packet), 0);
    
    printf("Folder transfer completed\n");
    close(sock_fd);
}

// Main function
int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Usage:\n");
        printf("Server mode: %s -s <port>\n", argv[0]);
        printf("Client mode: %s -c <folderpath> <destination_ip> <destination_port>\n", argv[0]);
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
            printf("Client mode usage: %s -c <folderpath> <destination_ip> <destination_port>\n", argv[0]);
            return 1;
        }
        send_folder(argv[2], argv[3], atoi(argv[4]));
    } else {
        printf("Invalid mode. Use -s for server or -c for client.\n");
        return 1;
    }
    
    return 0;
}