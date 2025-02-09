// backup.c
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <errno.h>

#define MAX_PATH_LENGTH 256
#define BUFFER_SIZE 4096

// Structure to hold file/directory information
typedef struct {
    char path[MAX_PATH_LENGTH];
    int is_directory;
} FileInfo;

void handle_storage_server_backup(int client_fd) {
    char ss_id[32];
    recv(client_fd, ss_id, sizeof(ss_id), 0);
    printf("[Backup Server] Handling backup for Storage Server: %s\n", ss_id);

    // Create directory for storage server
    char ss_dir[MAX_PATH_LENGTH];
    snprintf(ss_dir, sizeof(ss_dir), "./%s", ss_id);
    mkdir(ss_dir, 0777);

    // Receive number of paths
    int num_paths;
    recv(client_fd, &num_paths, sizeof(int), 0);
    printf("[Backup Server] Expecting %d paths\n", num_paths);

    // Process each path
    for (int i = 0; i < num_paths; i++) {
        // Receive path length and path
        int path_len;
        recv(client_fd, &path_len, sizeof(int), 0);
        
        char path[MAX_PATH_LENGTH];
        recv(client_fd, path, path_len, 0);
        printf("[Backup Server] Processing path: %s\n", path);

        // Create the base path directory
        char base_path[MAX_PATH_LENGTH];
        snprintf(base_path, sizeof(base_path), "%s/%s", ss_dir, path);
        mkdir(base_path, 0777);

        // Receive files and directories
        while (1) {
            FileInfo file_info;
            ssize_t bytes_received = recv(client_fd, &file_info, sizeof(FileInfo), 0);
            
            if (bytes_received <= 0) {
                break;
            }

            // Check for completion signal
            if (strcmp(file_info.path, "BACKUP_COMPLETE") == 0) {
                break;
            }

            char full_path[MAX_PATH_LENGTH];
            snprintf(full_path, sizeof(full_path), "%s%s", ss_dir, file_info.path);

            if (file_info.is_directory) {
                mkdir(full_path, 0777);
                printf("[Backup Server] Created directory: %s\n", full_path);
                continue;
            }

            // Receive file size
            off_t file_size;
            recv(client_fd, &file_size, sizeof(off_t), 0);

            // Create parent directories if they don't exist
            char parent_dir[MAX_PATH_LENGTH];
            strcpy(parent_dir, full_path);
            char* last_slash = strrchr(parent_dir, '/');
            if (last_slash) {
                *last_slash = '\0';
                mkdir(parent_dir, 0777);
            }

            // Receive and save file contents
            FILE* file = fopen(full_path, "wb");
            if (!file) {
                perror("[Backup Server] Error creating file");
                continue;
            }

            printf("[Backup Server] Receiving file: %s (size: %ld bytes)\n", full_path, file_size);

            char buffer[BUFFER_SIZE];
            off_t remaining = file_size;
            while (remaining > 0) {
                size_t to_read = (remaining < BUFFER_SIZE) ? remaining : BUFFER_SIZE;
                bytes_received = recv(client_fd, buffer, to_read, 0);
                if (bytes_received <= 0) break;
                
                fwrite(buffer, 1, bytes_received, file);
                remaining -= bytes_received;
            }

            fclose(file);
            printf("[Backup Server] Completed file: %s\n", full_path);
        }
    }

    printf("[Backup Server] Backup completed for %s\n", ss_id);
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        printf("Usage: %s <port>\n", argv[0]);
        return 1;
    }

    int port = atoi(argv[1]);
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("Socket creation failed");
        return 1;
    }

    // Enable address reuse
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in server_addr = {0};
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(port);

    if (bind(server_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        perror("Bind failed");
        close(server_fd);
        return 1;
    }

    if (listen(server_fd, 5) < 0) {
        perror("Listen failed");
        close(server_fd);
        return 1;
    }

    printf("[Backup Server] Listening on port %d\n", port);

    while (1) {
        struct sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &client_len);
        
        if (client_fd < 0) {
            perror("Accept failed");
            continue;
        }

        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, INET_ADDRSTRLEN);
        printf("[Backup Server] New connection from %s\n", client_ip);

        handle_storage_server_backup(client_fd);
        close(client_fd);
    }

    close(server_fd);
    return 0;
}