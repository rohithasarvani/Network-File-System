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
#include <time.h>
#include <ifaddrs.h>

#define BUFFER_SIZE 4096
#define PATH_MAX 4096

// Packet types
#define PACKET_SS_ID 0           // Storage server ID
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

// Function to get timestamp string
void get_timestamp(char *buffer, size_t size) {
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    strftime(buffer, size, "%Y%m%d_%H%M%S", t);
}

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

// Function to create backup directory structure
char* create_backup_directory(const char *base_path, const char *ss_id) {
    static char backup_path[PATH_MAX];
    char timestamp[20];
    get_timestamp(timestamp, sizeof(timestamp));
    
    // Create main backup directory if it doesn't exist
    create_directory_recursive(base_path, 0755);
    
    // Create SS-specific directory
    snprintf(backup_path, PATH_MAX, "%s/%s", base_path, ss_id);
    create_directory_recursive(backup_path, 0755);
    
    // Create timestamped backup directory
    snprintf(backup_path, PATH_MAX, "%s/%s/%s", base_path, ss_id, timestamp);
    create_directory_recursive(backup_path, 0755);
    
    return backup_path;
}

void find_ss_ip(char *ip) {
     struct ifaddrs *ifaddr, *ifa;

    // Retrieve the linked list of network interfaces
    if (getifaddrs(&ifaddr) == -1) {
        perror("getifaddrs");
        exit(EXIT_FAILURE);
    }

    // Loop through the list of interfaces
    for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
        if (ifa->ifa_addr == NULL) {
            continue;
        }

        // Check for IPv4 addresses only and ignore loopback addresses
        if (ifa->ifa_addr->sa_family == AF_INET) {
            // Get the address in a human-readable form
            void *addr = &((struct sockaddr_in *)ifa->ifa_addr)->sin_addr;
            inet_ntop(AF_INET, addr, ip, INET_ADDRSTRLEN);

            // Skip loopback addresses (starting with "127.")
            if (strncmp(ip, "127.", 4) != 0) {
                break;  // Stop after finding the first non-loopback IPv4 address
            }
        }
    }

    // Free the linked list of network interfaces
    freeifaddrs(ifaddr);
    // char hostname[256];
    // gethostname(hostname, sizeof(hostname));
    // struct hostent *host_entry;
    // host_entry = gethostbyname(hostname);
    // strcpy(ip, inet_ntoa(*((struct in_addr*)host_entry->h_addr_list[0])));
}
// Function to handle a single client connection
void handle_client(int client_fd, const char *backup_base_path) {
    FILE *fp = NULL;
    struct TransferPacket packet;
    char current_backup_path[PATH_MAX] = {0};
    char ss_id[256] = {0};
    int received_ss_id = 0;
    
    while (1) {
        ssize_t bytes_received = recv(client_fd, &packet, sizeof(packet), 0);
        if (bytes_received <= 0) break;
        
        // Handle SS ID first
        if (packet.type == PACKET_SS_ID && !received_ss_id) {
            strncpy(ss_id, packet.data, sizeof(ss_id) - 1);
            received_ss_id = 1;
            strcpy(current_backup_path, create_backup_directory(backup_base_path, ss_id));
            printf("Created backup directory for SS ID: %s\n", ss_id);
            continue;
        }
        
        // Ensure we have received SS ID before processing other packets
        if (!received_ss_id) {
            printf("Error: No SS ID received\n");
            break;
        }
        
        char full_path[PATH_MAX];
        
        switch (packet.type) {
            case PACKET_DIR_CREATE: {
                snprintf(full_path, sizeof(full_path), "%s/%s", current_backup_path, packet.path);
                printf("Creating directory: %s\n", full_path);
                create_directory_recursive(full_path, packet.mode);
                break;
            }
            
            case PACKET_FILE_START: {
                snprintf(full_path, sizeof(full_path), "%s/%s", current_backup_path, packet.path);
                printf("Receiving file: %s\n", full_path);
                
                // Create parent directories if they don't exist
                char *dir_path = strdup(full_path);
                create_directory_recursive(dirname(dir_path), 0755);
                free(dir_path);
                
                if (fp) fclose(fp);
                fp = fopen(full_path, "wb");
                if (!fp) {
                    perror("File creation failed");
                    continue;
                }
                chmod(full_path, packet.mode);
                break;
            }
            
            case PACKET_FILE_DATA: {
                if (fp) {
                    fwrite(packet.data, 1, packet.size, fp);
                }
                break;
            }
            
            case PACKET_FILE_END: {
                if (fp) {
                    fclose(fp);
                    fp = NULL;
                    printf("File completed\n");
                }
                break;
            }
            
            case PACKET_TRANSFER_DONE: {
                printf("Backup completed for SS ID: %s\n", ss_id);
                goto transfer_done;
            }
        }
    }
    
transfer_done:
    if (fp) fclose(fp);
}

// Main backup server function
void start_backup_server(int port, const char *backup_base_path) {
    int server_fd, client_fd;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_len = sizeof(client_addr);
    
    // Create socket
    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1) {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }
    
    // Set socket options for reuse
    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))) {
        perror("setsockopt failed");
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
    if (listen(server_fd, 10) < 0) {
        perror("Listen failed");
        exit(EXIT_FAILURE);
    }
    
    printf("Backup server listening on port %d...\n", port);
    printf("Using backup directory: %s\n", backup_base_path);
    
    // Create base backup directory if it doesn't exist
    create_directory_recursive(backup_base_path, 0755);
    
    while (1) {
        client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_len);
        if (client_fd < 0) {
            perror("Accept failed");
            continue;
        }
        
        printf("Client connected from %s:%d\n", 
               inet_ntoa(client_addr.sin_addr), 
               ntohs(client_addr.sin_port));
        
        // Handle client connection
        handle_client(client_fd, backup_base_path);
        
        close(client_fd);
    }
    
    close(server_fd);
}

int main(int argc, char *argv[]) {
    if (argc != 3) {
        printf("Usage: %s <port> <backup_directory>\n", argv[0]);
        return 1;
    }
    
    int port = atoi(argv[1]);
    const char *backup_dir = argv[2];
    char ip[INET_ADDRSTRLEN];
    find_ss_ip(ip);
    printf("Running on %s\n",ip);
    start_backup_server(port, backup_dir);
    
    return 0;
}