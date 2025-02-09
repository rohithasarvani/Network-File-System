#include "storage_server.h"
// #include "naming_server.h"

char *NS_IP;
int NS_port;
int NS_sock;
#define ACK_BUFFER_SIZE 512
// Function to check if path is a directory
int is_directory(const char *path) {
    struct stat path_stat;
    if (stat(path, &path_stat) == 0) {
        return S_ISDIR(path_stat.st_mode);
    }
    return 0;
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
 // Modified scan_directory_structure function
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
    
    // Create the source directory itself first
    memset(&packet, 0, sizeof(packet));
    packet.type = PACKET_DIR_CREATE;
    packet.mode = S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH;  // Default directory permissions
    
    // Use basename of the source directory as the first directory to create
    char *source_dir_name = basename(strdup(base_path));
    strncpy(packet.base_dir, source_dir_name, PATH_MAX - 1);
    strncpy(packet.path, source_dir_name, PATH_MAX - 1);
    send(sock_fd, &packet, sizeof(packet), 0);
    
    while ((entry = readdir(dir))) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;
            
        snprintf(full_path, sizeof(full_path), "%s/%s", dir_path, entry->d_name);
        if (stat(full_path, &path_stat) == -1)
            continue;
            
        if (S_ISDIR(path_stat.st_mode)) {
            // For subdirectories, include the source directory name in the path
            memset(&packet, 0, sizeof(packet));
            packet.type = PACKET_DIR_CREATE;
            packet.mode = path_stat.st_mode;
            strncpy(packet.base_dir, source_dir_name, PATH_MAX - 1);
            
            // Construct relative path including source directory
            char relative_path[PATH_MAX];
            snprintf(relative_path, PATH_MAX, "%s/%s", source_dir_name, 
                    get_relative_path(base_path, full_path));
            strncpy(packet.path, relative_path, PATH_MAX - 1);
            
            send(sock_fd, &packet, sizeof(packet), 0);
            scan_directory_structure(sock_fd, base_path, full_path);
        }
    }
    
    closedir(dir);
}

// Modified send_directory_contents function
void send_directory_contents(int sock_fd, const char *base_path, const char *dir_path) {
    DIR *dir;
    struct dirent *entry;
    struct stat path_stat;
    char full_path[PATH_MAX];
    struct TransferPacket packet;
    
    dir = opendir(dir_path);
    if (!dir) {
        printf("Error opening directory (ERROR CODE %d)\n",ERR_OPENING);
        perror("Failed to open directory");
        return;
    }
    
    char *source_dir_name = basename(strdup(base_path));
    
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
            
            // Include source directory in base_dir
            strncpy(packet.base_dir, source_dir_name, PATH_MAX - 1);
            
            // Construct relative path including source directory
            char relative_path[PATH_MAX];
            snprintf(relative_path, PATH_MAX, "%s/%s", source_dir_name, 
                    get_relative_path(base_path, full_path));
            strncpy(packet.path, relative_path, PATH_MAX - 1);
            
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

// Modified send_single_file function for consistency
void send_single_file(int sock_fd, const char *file_path, const char *ss_id) {
    struct TransferPacket packet;
    struct stat path_stat;
    FILE *fp;

    if (stat(file_path, &path_stat) == -1) {
        printf("Failed to get file stats for: %s\n", file_path);
        return;
    }

    fp = fopen(file_path, "rb");
    if (!fp) {
        printf("Error Failed to open file (ERROR CODE %d)\n",ERR_OPENING);
        printf("Failed to open file: %s\n", file_path);
        return;
    }

    // Send file start packet
    memset(&packet, 0, sizeof(packet));
    packet.type = PACKET_FILE_START;
    packet.mode = path_stat.st_mode;
    strncpy(packet.base_dir, ss_id, PATH_MAX - 1);
    
    // Use the full filename as the path
    char *filename = basename(strdup(file_path));
    strncpy(packet.path, filename, PATH_MAX - 1);
    
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
    printf("Sent file: %s\n", file_path);
}
// // Function to send a single file
// void send_single_file(int sock_fd, const char *file_path, const char *base_name) {
//     struct TransferPacket packet;
//     struct stat path_stat;
//     FILE *fp;

//     if (stat(file_path, &path_stat) == -1) {
//         printf("Failed to get file stats for: %s\n", file_path);
//         return;
//     }

//     fp = fopen(file_path, "rb");
//     if (!fp) {
//         printf("Failed to open file: %s\n", file_path);
//         return;
//     }

//     // Send file start packet
//     memset(&packet, 0, sizeof(packet));
//     packet.type = PACKET_FILE_START;
//     packet.mode = path_stat.st_mode;
//     strncpy(packet.base_dir, base_name, PATH_MAX - 1);
//     strncpy(packet.path, basename(strdup(file_path)), PATH_MAX - 1);
//     send(sock_fd, &packet, sizeof(packet), 0);

//     // Send file data
//     while (!feof(fp)) {
//         memset(&packet.data, 0, BUFFER_SIZE);
//         packet.type = PACKET_FILE_DATA;
//         packet.size = fread(packet.data, 1, BUFFER_SIZE, fp);
//         send(sock_fd, &packet, sizeof(packet), 0);
//     }

//     // Send file end packet
//     packet.type = PACKET_FILE_END;
//     send(sock_fd, &packet, sizeof(packet), 0);

//     fclose(fp);
//     printf("Sent file: %s\n", file_path);
// }

// // Function to scan and create directory structure
// void scan_directory_structure(int sock_fd, const char *base_path, const char *dir_path) {
//     DIR *dir;
//     struct dirent *entry;
//     struct stat path_stat;
//     char full_path[PATH_MAX];
//     struct TransferPacket packet;
    
//     dir = opendir(dir_path);
//     if (!dir) {
//         perror("Failed to open directory");
//         return;
//     }
    
//     // Send directory creation packet
//     memset(&packet, 0, sizeof(packet));
//     packet.type = PACKET_DIR_CREATE;
//     packet.mode = S_IRWXU | S_IRWXG | S_IROTH | S_IXOTH;  // Default directory permissions
//     strncpy(packet.base_dir, basename(strdup(base_path)), PATH_MAX - 1);
//     strncpy(packet.path, get_relative_path(base_path, dir_path), PATH_MAX - 1);
//     send(sock_fd, &packet, sizeof(packet), 0);
    
//     while ((entry = readdir(dir))) {
//         if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
//             continue;
            
//         snprintf(full_path, sizeof(full_path), "%s/%s", dir_path, entry->d_name);
//         if (stat(full_path, &path_stat) == -1)
//             continue;
            
//         if (S_ISDIR(path_stat.st_mode)) {
//             scan_directory_structure(sock_fd, base_path, full_path);
//         }
//     }
    
//     closedir(dir);
// }

// // Function to send directory contents after structure is created
// void send_directory_contents(int sock_fd, const char *base_path, const char *dir_path) {
//     DIR *dir;
//     struct dirent *entry;
//     struct stat path_stat;
//     char full_path[PATH_MAX];
//     struct TransferPacket packet;
    
//     dir = opendir(dir_path);
//     if (!dir) {
//         perror("Failed to open directory");
//         return;
//     }
    
//     while ((entry = readdir(dir))) {
//         if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
//             continue;
            
//         snprintf(full_path, sizeof(full_path), "%s/%s", dir_path, entry->d_name);
//         if (stat(full_path, &path_stat) == -1)
//             continue;
            
//         if (S_ISDIR(path_stat.st_mode)) {
//             send_directory_contents(sock_fd, base_path, full_path);
//         } else if (S_ISREG(path_stat.st_mode)) {
//             FILE *fp = fopen(full_path, "rb");
//             if (!fp) continue;
            
//             // Send file start packet
//             memset(&packet, 0, sizeof(packet));
//             packet.type = PACKET_FILE_START;
//             packet.mode = path_stat.st_mode;
//             strncpy(packet.base_dir, basename(strdup(base_path)), PATH_MAX - 1);
//             strncpy(packet.path, get_relative_path(base_path, full_path), PATH_MAX - 1);
//             send(sock_fd, &packet, sizeof(packet), 0);
            
//             // Send file data
//             while (!feof(fp)) {
//                 memset(&packet.data, 0, BUFFER_SIZE);
//                 packet.type = PACKET_FILE_DATA;
//                 packet.size = fread(packet.data, 1, BUFFER_SIZE, fp);
//                 send(sock_fd, &packet, sizeof(packet), 0);
//             }
            
//             // Send file end packet
//             packet.type = PACKET_FILE_END;
//             send(sock_fd, &packet, sizeof(packet), 0);
            
//             fclose(fp);
//             printf("Sent file: %s\n", full_path);
//         }
//     }
    
//     closedir(dir);
// }
void send_backup_to_server(const char* backup_ip, int backup_port, const char* ss_id, const char** paths, int num_paths) {
    int backup_sock = 0;
    struct sockaddr_in serv_addr;
    struct TransferPacket packet;

    printf("Backup IP: %s\n", backup_ip);
    printf("Backup Port: %d\n", backup_port);

    // Create socket
    if ((backup_sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        printf("socket creation error (ERROR CODE %d)\n",ERR_SOCK);
        // printf("Socket creation error\n");
        return;
    }

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(backup_port);

    // Convert IPv4 address from text to binary
    if (inet_pton(AF_INET, backup_ip, &serv_addr.sin_addr) <= 0) {
        printf("Invalid address/ Address not supported\n");
        close(backup_sock);
        return;
    }

    // Connect to the backup server
    if (connect(backup_sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        printf("socket connection error (ERROR CODE %d)\n",ERR_SOCK_CONNECT);
        // printf("Connection Failed\n");
        close(backup_sock);
        return;
    }

    // Send storage server ID
    memset(&packet, 0, sizeof(packet));
    packet.type = PACKET_SS_ID;
    strncpy(packet.data, ss_id, BUFFER_SIZE - 1);
    send(backup_sock, &packet, sizeof(packet), 0);

    // Process each path
    for (int i = 0; i < num_paths; i++) {
        if (is_directory(paths[i])) {
            // Handle directory backup
            printf("Backing up directory: %s\n", paths[i]);
            
            // First, send directory structure
            scan_directory_structure(backup_sock, paths[i], paths[i]);
            
            // Then, send all files
            send_directory_contents(backup_sock, paths[i], paths[i]);
        } else {
            // Handle single file backup
            printf("Backing up file: %s\n", paths[i]);
            send_single_file(backup_sock, paths[i], ss_id);
        }
    }

    // Send transfer completion packet
    memset(&packet, 0, sizeof(packet));
    packet.type = PACKET_TRANSFER_DONE;
    send(backup_sock, &packet, sizeof(packet), 0);

    close(backup_sock);
    printf("Backup completed for all paths\n");
}


int is_path_valid(const char* path){
    return access(path,F_OK) == 0;
}

int is_file(const char* path){
    struct stat path_stat;
    stat(path,&path_stat);
    return S_ISREG(path_stat.st_mode);
}
void send_file_info(int client_socket, const char *filename) {
    struct stat st;
    char buffer[512];  // Buffer to store message

    if (stat(filename, &st) == 0) {
        // File size
        off_t size = st.st_size;

        // File type
        const char *file_type;
        if (S_ISREG(st.st_mode)) {
            file_type = "Regular File";
        } else if (S_ISDIR(st.st_mode)) {
            file_type = "Directory";
        } else if (S_ISLNK(st.st_mode)) {
            file_type = "Symbolic Link";
        } else if (S_ISCHR(st.st_mode)) {
            file_type = "Character Device";
        } else if (S_ISBLK(st.st_mode)) {
            file_type = "Block Device";
        } else if (S_ISFIFO(st.st_mode)) {
            file_type = "FIFO (Pipe)";
        } else if (S_ISSOCK(st.st_mode)) {
            file_type = "Socket";
        } else {
            file_type = "Unknown";
        }

        // Human-readable permissions
        char permissions[10];
        snprintf(permissions, sizeof(permissions), "%c%c%c%c%c%c%c%c%c",
                 (st.st_mode & S_IRUSR) ? 'r' : '-',
                 (st.st_mode & S_IWUSR) ? 'w' : '-',
                 (st.st_mode & S_IXUSR) ? 'x' : '-',
                 (st.st_mode & S_IRGRP) ? 'r' : '-',
                 (st.st_mode & S_IWGRP) ? 'w' : '-',
                 (st.st_mode & S_IXGRP) ? 'x' : '-',
                 (st.st_mode & S_IROTH) ? 'r' : '-',
                 (st.st_mode & S_IWOTH) ? 'w' : '-',
                 (st.st_mode & S_IXOTH) ? 'x' : '-');

        // Last modified time
        char mod_time[20];
        strftime(mod_time, sizeof(mod_time), "%Y-%m-%d %H:%M:%S", localtime(&st.st_mtime));

        // Format information into buffer
        snprintf(buffer, sizeof(buffer), 
                 "Size: %ld bytes\nPermissions: %s\nFile Type: %s\nLast Modified: %s\n",
                 size, permissions, file_type, mod_time);
    } else {
        snprintf(buffer, sizeof(buffer), "Error: Unable to get info for file %s\n", filename);
    }

    // Send the buffer to the client
    send(client_socket, buffer, strlen(buffer), 0);
}
int count_files_in_directory(const char *directory_path) {
    int file_count = 0;
    struct dirent *entry;
    DIR *dp = opendir(directory_path);

    if (dp == NULL) {
        printf("Error Failed to open directory (ERROR CODE %d)\n",ERR_OPENING);
        return -1;  // Return -1 if the directory can't be opened
    }

    while ((entry = readdir(dp)) != NULL) {
        // Skip "." and ".."
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        // Construct the full path
        char full_path[BUFFER_SIZE];
        snprintf(full_path, sizeof(full_path), "%s/%s", directory_path, entry->d_name);

        if (entry->d_type == DT_DIR) {
            // If it's a directory, count files recursively
            int subdir_count = count_files_in_directory(full_path);
            if (subdir_count < 0) {
                closedir(dp);
                return -1;  // Return error if any subdirectory can't be accessed
            }
            file_count += subdir_count;
        } else if (entry->d_type == DT_REG) {
            // Count files
            file_count++;
        }
    }

    closedir(dp);
    return file_count;
}

// Function to delete all files and subdirectories inside a directory (recursive)
int delete_directory_contents(const char *directory_path) {
    struct dirent *entry;
    DIR *dp = opendir(directory_path);

    if (dp == NULL) {
        printf("Error Failed to open directory (ERROR CODE %d)\n",ERR_OPENING);
        return -1;  // Return -1 if the directory can't be opened
    }

    while ((entry = readdir(dp)) != NULL) {
        // Skip "." and ".."
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }

        // Construct the full path
        char full_path[BUFFER_SIZE];
        snprintf(full_path, sizeof(full_path), "%s/%s", directory_path, entry->d_name);

        if (entry->d_type == DT_DIR) {
            // If it's a directory, delete its contents recursively
            if (delete_directory_contents(full_path) != 0) {
                closedir(dp);
                return -1;  // Return error if any subdirectory couldn't be deleted
            }
            // Now delete the empty subdirectory
            if (rmdir(full_path) != 0) {
                closedir(dp);
                return -1;  // Return error if the directory couldn't be deleted
            }
        } else {
            // If it's a file, delete it
            if (remove(full_path) != 0) {
                closedir(dp);
                return -1;  // Return error if the file couldn't be deleted
            }
        }
    }

    closedir(dp);
    return 0;
}
// Function to handle the DELETE command
void handle_delete_command(const char *path, char *response, int client_socket) {
    if (!is_path_valid(path)) {
        snprintf(response, BUFFER_SIZE, "Error: Path '%s' does not exist.", path);
        send(client_socket, response, strlen(response), 0);
        return;
    }

    if (is_file(path)) {
        // File deletion confirmation
        // snprintf(response, BUFFER_SIZE, "Are you sure you want to delete the file '%s'? (yes/no)", path);
        // send(client_socket, response, strlen(response), 0);

        // char client_confirmation[BUFFER_SIZE];
        // recv(client_socket, client_confirmation, BUFFER_SIZE, 0);
         //if (strncmp(client_confirmation, "yes", 3) == 0) {
            if (remove(path) == 0) {
                snprintf(response, BUFFER_SIZE, "File '%s' deleted successfully.", path);
            } else {
                snprintf(response, BUFFER_SIZE, "Error: Could not delete file '%s'.", path);
            }
       // } 
        // else {
        //     snprintf(response, BUFFER_SIZE, "File deletion cancelled.");
        // }
    } else {
        // Directory deletion confirmation
        int file_count = count_files_in_directory(path);
        if (file_count < 0) {
            snprintf(response, BUFFER_SIZE, "Error: Unable to access directory '%s'.", path);
            send(client_socket, response, strlen(response), 0);
            return;
        }
        // snprintf(response, BUFFER_SIZE, "Directory '%s' contains %d items. Are you sure you want to delete it and all its contents? (yes/no)", path, file_count);
        // send(client_socket, response, strlen(response), 0);

        //char client_confirmation[BUFFER_SIZE];
       // recv(client_socket, client_confirmation, BUFFER_SIZE, 0);
        //if (strncmp(client_confirmation, "yes", 3) == 0) {
            if (file_count == 0) {
                // If directory is empty, just delete it
                if (rmdir(path) == 0) {
                    snprintf(response, BUFFER_SIZE, "Directory '%s' deleted successfully.", path);
                } else {
                    snprintf(response, BUFFER_SIZE, "Error: Could not delete directory '%s'.", path);
                }
            } else {
                // Delete all contents first, then delete the directory itself
                if (delete_directory_contents(path) == 0 && rmdir(path) == 0) {
                    snprintf(response, BUFFER_SIZE, "Directory '%s' and all its contents deleted successfully.", path);
                } else {
                    snprintf(response, BUFFER_SIZE, "Error: Could not delete directory '%s' or its contents.", path);
                }
            }

            
       // }
        //  else {
        //     snprintf(response, BUFFER_SIZE, "Directory deletion cancelled.");
        // }
    }
printf("this is message:%s",response);
    // Send the final response to the client
    send(client_socket, response, strlen(response), 0);
}
// Function to handle the CREATE command
void handle_create_command(const char *path, const char *name, const char type, char *response) {
    char full_path[BUFFER_SIZE];
    snprintf(full_path, sizeof(full_path), "%s/%s", path, name);

    // Check if the given path is a file
    if (is_file(path)) {
        snprintf(response, BUFFER_SIZE, "Error: The given path for create operation leads to a file.");
        return;
    }

    // Check if we need to create a file or directory
    if (type == 'F') {
        FILE *file = fopen(full_path, "w");
        if (file == NULL) {
            printf("Error Failed to create file (ERROR CODE %d)\n",ERR_FAILED_TO_CREATE);
            snprintf(response, BUFFER_SIZE, "Error: Failed to create file at %s.", full_path);
        } else {
            fclose(file);
            snprintf(response, BUFFER_SIZE, "Success in creating file at %s.", full_path);
        }
    } else if (type == 'D') {
        if (mkdir(full_path, 0777) != 0) {
            printf("Error Failed to create directory (ERROR CODE %d)\n",ERR_FAILED_TO_CREATE);
            snprintf(response, BUFFER_SIZE, "Error: Failed to create directory at %s.", full_path);
        } else {
            snprintf(response, BUFFER_SIZE, "Success in created at %s.", full_path);
        }
    } else {
        snprintf(response, BUFFER_SIZE, "Error: Invalid type. Use 'F' for file or 'D' for directory.");
    }

}
////////////////////////////////////////////
void send_completion_ack_to_ns(const char* filename, const char* client_ip, 
                             int client_port, const char* result) {
    int ns_socket;
    struct sockaddr_in ns_addr;
    char ack_buffer[ACK_BUFFER_SIZE];
    
    // Create socket for NS communication
    ns_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (ns_socket < 0) {
        printf("socket error (ERROR CODE %d)\n",ERR_SOCK);
        perror("Error creating socket for NS communication");
        return;
    }
    
    // Configure NS address
    ns_addr.sin_family = AF_INET;
    ns_addr.sin_port = htons(NS_port);
    if (inet_pton(AF_INET, NS_IP, &ns_addr.sin_addr) <= 0) {
        perror("Invalid address");
        close(ns_socket);
        return;
    }
    
    // Connect to NS
    if (connect(ns_socket, (struct sockaddr*)&ns_addr, sizeof(ns_addr)) < 0) {
        printf("socket connection error (ERROR CODE %d)\n",ERR_SOCK);
        perror("Error connecting to Naming Server");
        close(ns_socket);
        return;
    }
    
    // Prepare completion acknowledgment message
    snprintf(ack_buffer, sizeof(ack_buffer), 
             "ASYNCWRITE_COMPLETE %s %s %d %s", 
             filename, client_ip, client_port, result);
    
    // Send acknowledgment to NS
    send(ns_socket, ack_buffer, strlen(ack_buffer), 0);
    
    // Close NS connection
    close(ns_socket);
    printf("Sent completion acknowledgment to NS for file: %s\n", filename);
}

void* async_write_task(void *arg) {
    // Cast the argument to the correct structure type
    char buffer1[BUFFER_SIZE];
    WriteTaskArgs *task_args = (WriteTaskArgs*)arg;
    char *filename = task_args->filename;
    char *data = task_args->data;
    char result[BUFFER_SIZE];


    printf("Asynchronous write task started for file: '%s'\n", filename);
    fflush(stdout);

    // Open the file to write asynchronously
    FILE *file = fopen(filename, "w");  // Open in append mode
    if (file) {
        // Simulate chunked writing (flush periodically)
        int chunk_size = 1024;  // Adjust chunk size as needed
        int data_len = strlen(data);
        int written = 0;
        while (written < data_len) {
            int chunk = (data_len - written > chunk_size) ? chunk_size : (data_len - written);
            fwrite(data + written, 1, chunk, file);
            written += chunk;
            fflush(file);  // Flush periodically to disk
        }
        fclose(file);

        // After write is complete, you can optionally notify the Naming Server or client (if needed)
        // printf("Asynchronous write completed for file: %s\n", filename);
        snprintf(result, sizeof(result), "ASYNC WRITE SUCCESS, File written successfully");
        printf("Asynchronous write completed for file: %s client\n", task_args->filename);
        // Notify NS of the write completion
        
        // snprintf(buffer1, sizeof(buffer1), "Async Success: Data written Asynchronously to %s\n", filename);
        // send()
    } else {
        snprintf(result, sizeof(result), "ERROR Failed to write file");
        printf("Error Failed to open file (ERROR CODE %d)\n",ERR_OPENING);
        perror("Failed to open file for asynchronous writing");
    }

    send_completion_ack_to_ns(task_args->filename, 
                            task_args->client_ip,
                            task_args->client_port,
                            result);

    // Free the allocated memory for task_args
    // free(task_args->filename);
    // free(task_args->data);
    // free(task_args->client_ip);
    // free(task_args->client_port);
    free(task_args);
    return NULL;
}
/////////////////////////////////////////////////////////////////////
void handle_client_request(char* buffer, char*command, char*filename, int client_socket) {
    char buffer1[BUFFER_SIZE];
    // memset(buffer, 0, sizeof(buffer));
    // recv(client_socket, buffer, sizeof(buffer), 0);

    // char command[10];
    // char filename[BUFFER_SIZE];
    
    // sscanf(buffer, "%s %s", command, filename);

    struct stat path_stat;
    int is_directory = (stat(filename, &path_stat) == 0 && S_ISDIR(path_stat.st_mode));

    if (strcmp(command, "READ") == 0) {
        if (is_directory) {
            // If the path is a directory, list contents
            DIR *dir = opendir(filename);
            if (dir) {
                struct dirent *entry;
                snprintf(buffer1, sizeof(buffer1), "Directory contents of %s:\n", filename);
                send(client_socket, buffer1, strlen(buffer1), 0);
                
                while ((entry = readdir(dir)) != NULL) {
                    snprintf(buffer, sizeof(buffer), "%s\n", entry->d_name);
                    send(client_socket, buffer, strlen(buffer), 0);
                }
                closedir(dir);
            } else {
                printf("Error Failed to read directory (ERROR CODE %d)\n",ERR_FAILED_TO_WRITE);
                snprintf(buffer, sizeof(buffer), "Error: Unable to open directory %s\n", filename);
                send(client_socket, buffer, strlen(buffer), 0);
            }
        } else {
            // Read file and send contents to client
            FILE *file = fopen(filename, "r");
            if (file) {
                while (fgets(buffer, sizeof(buffer), file)) {
                    send(client_socket, buffer, strlen(buffer), 0);
                }
                fclose(file);
            } else {
                printf("Error Failed to read file (ERROR CODE %d)\n",ERR_FAILED_TO_READ);
                snprintf(buffer, sizeof(buffer), "Error: Unable to read file %s\n", filename);
                send(client_socket, buffer, strlen(buffer), 0);
            }
        }
    }else if (strcmp(command, "WRITE") == 0) {
        if (is_directory) {
            // printf(RED "Cannot perform WRITE on a directory (ERROR CODE %d)\n",ERR_IS_DIRECTORY);
            printf("Error Cannot perform WRITE on a directory (ERROR CODE %d)\n",ERR_IS_DIRECTORY);
            snprintf(buffer1, sizeof(buffer1), "Error: Cannot perform WRITE on a directory %s\n", filename);
            send(client_socket, buffer1, strlen(buffer1), 0);
        } else {
            // Extract filename and data
            char *data = strchr(buffer, ' ') + 1;  // Move to the filename part
            char *filename = data;
            data = strchr(data, ' ');
            
            if (data) {
                *data = '\0';           // Null-terminate the filename
                data = data + 1;        // Move to the data part
            }

            // Check if data is provided
            if (data && strlen(data) > 0) {
                int sync_flag = 1; // Default: SYNCHRONOUS
                int data_size = strlen(data);
                // printf("%d\n",data_size);
                if (data_size > ASYNC_THRESHOLD ) { 
                    sync_flag = 2;  // Asynchronous write
                    printf("async write\n");
                    fflush(stdout);
                    
                }
                if (sync_flag == 2) {
                    // Handle asynchronous write (immediate acknowledgment)
                    char ack_msg[] = "Asynchronous write request accepted.";
                    send(client_socket, ack_msg, strlen(ack_msg), 0);

                    // Start the asynchronous write in the background
                    pthread_t async_write_thread;

                    // Prepare arguments for the thread
                    WriteTaskArgs *args = malloc(sizeof(WriteTaskArgs));
                    args->filename = strdup(filename);  // Copy the filename
                    args->data = strdup(data);          // Copy the data

                    // Get client IP and port from socket
                    struct sockaddr_in addr;
                    socklen_t addr_len = sizeof(addr);
                    getpeername(client_socket, (struct sockaddr*)&addr, &addr_len);
                    inet_ntop(AF_INET, &addr.sin_addr, args->client_ip, INET_ADDRSTRLEN);
                    args->client_port = ntohs(addr.sin_port);
                

                    pthread_create(&async_write_thread, NULL, async_write_task, (void *)args);
                    pthread_detach(async_write_thread);  // Detach the thread to run independently
                }
                else{
                    FILE *file = fopen(filename, "w");
                    if (file) {
                        fprintf(file, "%s", data);  // Write data to file
                        fclose(file);
                        snprintf(buffer1, sizeof(buffer1), "Success: Data written to %s\n", filename);
                    } else {
                        printf("Error Failed to write to file (ERROR CODE %d)\n",ERR_FAILED_TO_WRITE);
                        snprintf(buffer1, sizeof(buffer1), "Error: Unable to write to file %s\n", filename);
                    }
                }
                
            } else {
                
                snprintf(buffer1, sizeof(buffer1), "Error: No data provided for WRITE command\n");
            }
            send(client_socket, buffer1, strlen(buffer1), 0);
        }
    } else if (strcmp(command, "APPEND") == 0) {
        // printf("hell0\n");
        if (is_directory) {
            printf("Error Cannot perform APPEND on a directory (ERROR CODE %d)\n",ERR_IS_DIRECTORY);
            snprintf(buffer1, sizeof(buffer1), "Error: Cannot perform APPEND on a directory %s\n", filename);
            send(client_socket, buffer1, strlen(buffer1), 0);
        } else {
            printf("entered file\n");
            // Extract filename and data
            char *data = strchr(buffer, ' ') + 1;  // Move to the filename part
            char *filename = data;
            data = strchr(data, ' ');
            
            if (data) {
                *data = '\0';           // Null-terminate the filename
                data = data + 1;        // Move to the data part
            }

            // Check if data is provided
            if (data && strlen(data) > 0) {
                FILE *file = fopen(filename, "a");  // Open in append mode
                if (file) {
                    fprintf(file, "%s", data);  // Append data to file
                    fclose(file);
                    snprintf(buffer1, sizeof(buffer1), "Success: Data appended to %s\n", filename);
                    printf("written\n");
                } else {
                    printf("Error Failed to APPEND to file (ERROR CODE %d)\n",ERR_FAILED_TO_APPEND);
                    snprintf(buffer1, sizeof(buffer1), "Error: Unable to append to file %s\n", filename);
                }
            } else {
                snprintf(buffer1, sizeof(buffer1), "Error: No data provided for APPEND command\n");
            }
            send(client_socket, buffer1, strlen(buffer1), 0);
        }
    } else if (strcmp(command, "INFO") == 0) {
                    send_file_info(client_socket, filename);
            // Get file size and permissions
        // }
    } 
    //close(client_socket);
}

int handle_audio_request(int client_sock, const char* filename) {
    FILE* audio_file = fopen(filename, "rb");
    if (!audio_file) {
        struct stream_response resp = {.status_code = FILE_NOT_FOUND};
        send(client_sock, &resp, sizeof(resp), 0);
        return -1;
    }

    // Get file size
    fseek(audio_file, 0, SEEK_END);
    long file_size = ftell(audio_file);
    fseek(audio_file, 0, SEEK_SET);

    // Send metadata
    struct audio_metadata metadata;
    strncpy(metadata.filename, filename, MAX_FILENAME);
    metadata.file_size = file_size;
    strcpy(metadata.mime_type, "audio/mpeg");  // Adjust based on file type
    send(client_sock, &metadata, sizeof(metadata), 0);

    // Stream file in chunks
    unsigned char buffer[CHUNK_SIZE];
    long total_sent = 0;
    while (total_sent < file_size) {
        size_t bytes_read = fread(buffer, 1, CHUNK_SIZE, audio_file);
        if (bytes_read > 0) {
            ssize_t bytes_sent = send(client_sock, buffer, bytes_read, 0);
            if (bytes_sent <= 0) {
                printf("Error Failed to stream audio (ERROR CODE %d)\n",ERR_FAILED_TO_STREAM_AUDIO);
                perror("Error sending audio data");
                fclose(audio_file);
                return -1;
            }
            total_sent += bytes_sent;
        } else if (feof(audio_file)) {
            break;
        } else {
            printf("Error Failed to stream audio (ERROR CODE %d)\n",ERR_FAILED_TO_STREAM_AUDIO);
            perror("Error reading audio file");
            fclose(audio_file);
            return -1;
        }
    }
    printf("Sent audio file %s (%ld bytes) to client\n", filename, file_size);

    fclose(audio_file);
    return 0;
}

// Function to connect to the naming server and send metadata, client port, and accessible paths
void connect_to_ns(const char *ns_ip, int ns_port, int client_port, const char *metadata, const char *paths[], int num_paths) {
    int sock;
    struct sockaddr_in ns_addr;
    char buffer[BUFFER_SIZE];
    char message[BUFFER_SIZE];
    
    // Create socket
    sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        printf("socket error (ERROR CODE %d)\n",ERR_SOCK);
        perror("Error creating socket");
        exit(EXIT_FAILURE);
    }

    // Configure naming server address structure
    ns_addr.sin_family = AF_INET;
    ns_addr.sin_port = htons(ns_port);
    if (inet_pton(AF_INET, ns_ip, &ns_addr.sin_addr) <= 0) {
        perror("Invalid address");
        close(sock);
        exit(EXIT_FAILURE);
    }

    // Connect to the naming server
    if (connect(sock, (struct sockaddr*)&ns_addr, sizeof(ns_addr)) < 0) {
        printf("socket connection error (ERROR CODE %d)\n",ERR_SOCK_CONNECT);
        perror("Connection failed");
        close(sock);
        exit(EXIT_FAILURE);
    }

    printf("Connected to Naming Server at %s:%d\n", ns_ip, ns_port);

    // Create a message with metadata, client port, and accessible paths
    snprintf(message, sizeof(message), "Metadata: %s, Client Port: %d", metadata, client_port);
    strcat(message, ", Paths:");
    int path_count = 0;
    for (int i = 0; i < num_paths; ++i) {
        struct stat statbuf;
        if (stat(paths[i], &statbuf) == 0) {
            if (S_ISDIR(statbuf.st_mode)) {
                printf("hello");
                // If the path is a directory, add its contents (including the directory itself)
                add_paths_recursive(paths[i], message, &path_count);
            } else if (S_ISREG(statbuf.st_mode)) {
                // If the path is a regular file, add it directly
                strcat(message, " ");
                strcat(message, paths[i]);
                path_count++;
            }
        } else {
            fprintf(stderr, "Invalid path: %s\n", paths[i]);
        }
    }

    printf("Total paths added: %d\n", path_count); // Print the total count of paths added

    // Send the message to the naming server
    send(sock, message, strlen(message), 0);
    printf("Sent message: %s\n", message);

    //Receive acknowledgment from the naming server
    memset(buffer, 0, sizeof(buffer));
    recv(sock, buffer, sizeof(buffer), 0);
    printf("Received acknowledgment: %s\n", buffer);
    char* check_msg = "Storage server listening on";
    send(sock, check_msg, strlen(check_msg), 0);

    // Close the connection
    //close(sock);
}
// Function to establish a connection, send data, and receive data
int connect_to_ss_and_func(const char *dest_ip, int dest_port, const char *message) {
    // Create a socket
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        printf("socket error (ERROR CODE %d)\n",ERR_SOCK);
        perror("Socket creation failed");
        return -1;
    }

    // Define the server address
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(dest_port);

    // Convert IP address from text to binary form
    if (inet_pton(AF_INET, dest_ip, &server_addr.sin_addr) <= 0) {
        perror("Invalid address or address not supported");
        close(sockfd);
        return -1;
    }

    // Establish the connection
    if (connect(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        printf("socket connection error (ERROR CODE %d)\n",ERR_SOCK_CONNECT);
        perror("Connection failed");
        close(sockfd);
        return -1;
    }

    printf("Connected to req SS%s:%d\n", dest_ip, dest_port);

    // Send the message
    if (send(sockfd, message, strlen(message), 0) < 0) {
        printf("socket send error (ERROR CODE %d)\n",ERR_SOCK_SEND);
        perror("Send failed");
        close(sockfd);
        return -1;
    }
    printf("Function Message sent: %s\n", message);

   
    // Close the socket
    close(sockfd);
    printf("Connection closed\n");

    return 0; // Success
}

void copy_file(int client_socket, const char *src_path, const char *dest_path, 
               const char *dest_ip, int dest_port)
{  printf("hello iam inside");
     FILE *src_file = fopen(src_path, "rb");
    if(!src_file) {
        printf("Error Failed to open source file (ERROR CODE %d)\n",ERR_OPENING);
        send(client_socket, "Failed to open source file", 25, 0);
        return;
    }

    struct file_info fi;
    memset(&fi, 0, sizeof(fi));
    strcpy(fi.name, basename((char*)src_path));
    fi.type = 'F';

    // Get file size
    fseek(src_file, 0, SEEK_END);
    fi.size = ftell(src_file);
    fseek(src_file, 0, SEEK_SET);

    // // Connect to destination storage server
    // int dest_socket = connect_to_storage_server(dest_ip, dest_port);
    // if(dest_socket < 0) {
    //     fclose(src_file);
    //     send(client_socket, "Failed to connect to destination", 31, 0);
    //     return;
    // }

   // Send create command to destination
    char create_cmd[BUFFER_SIZE];
    sprintf(create_cmd, "CREATE %s %s F", dest_path, fi.name);
    send(client_socket, create_cmd, strlen(create_cmd), 0);

    // // Wait for create confirmation
    // char response[BUFFER_SIZE];
    // recv(client_socket, response, BUFFER_SIZE, 0);

    // // Read and send file data
    // size_t bytes_read;
    // while((bytes_read = fread(fi.data, 1, sizeof(fi.data), src_file)) > 0) {
    //     char write_cmd[BUFFER_SIZE + sizeof(fi.data)];
    //     sprintf(write_cmd, "WRITE %s/%s ", dest_path, fi.name);
    //     memcpy(write_cmd + strlen(write_cmd), fi.data, bytes_read);
    //     send(client_socket, write_cmd, strlen(write_cmd) + bytes_read, 0);
        
    //     // Wait for write confirmation
    //     recv(client_socket, response, BUFFER_SIZE, 0);
    // }

    // fclose(src_file);
    // close(client_socket);
    // send(client_socket, "File copy completed successfully", 31, 0);
}

// New functions for storage server

// Function to handle client requests
void* handle_client(void* arg) {
    struct client_info* client = (struct client_info*)arg;
    char buffer[BUFFER_SIZE];
    char client_ip[INET_ADDRSTRLEN];
    
    // Get client IP address
    inet_ntop(AF_INET, &(client->address.sin_addr), client_ip, INET_ADDRSTRLEN);
    printf("Handling client from %s:%d\n", client_ip, ntohs(client->address.sin_port));
    char * handle_msg = "Handling client request";
    send(client->socket, handle_msg, strlen(handle_msg), 0);
    // char received_msg[100];
    // recv(client->socket, received_msg, sizeof(received_msg), 0);
    // printf("Received message: %s\n", received_msg);

    while (1) {
        // Clear buffer
        memset(buffer, 0, BUFFER_SIZE);
        
        // Receive request
        ssize_t bytes_received = recv(client->socket, buffer, BUFFER_SIZE - 1, 0);
        //printf("Received request from %s:%d: %s\n", client_ip, ntohs(client->address.sin_port), buffer);
        if (bytes_received <= 0) {
            // Client disconnected or error
            break;
        }
        printf("Received request from %s:%d: %s\n", 
               client_ip, ntohs(client->address.sin_port), buffer);
        if(strncmp(buffer, "COPY", 4) == 0){
            char * inst = strtok(buffer, " ");
            char * source_path = strtok(NULL, " ");
            char* dest_path = strtok(NULL, " ");
            char * dest_ip = strtok(NULL,  " " );
            int dest_port = strtok(NULL, " ");
            char message[8000];
            snprintf(message, sizeof(message), "%s %s %s", inst, source_path, dest_path);
            printf("mmmmmm %s\n",message);
            // if(!source_path || !dest_path || !dest_ip || !dest_port) {
            //     send(client->socket, "Invalid COPY command format", 26, 0);
            //     break;
            // }
            printf("checking\n");
            //int succ= connect_to_ss_and_func(dest_ip, dest_port, message);
            copy_file(client->socket, source_path, dest_path, dest_ip, dest_port);
               // send(client->socket, "Success", 7,0);
            
            break;
            
        }
        if(strncmp(buffer, "CREATE", 6) == 0){
            char * inst = strtok(buffer, " ");
            char *path = strtok(NULL, " ");
            char* name = strtok(NULL, " ");
            char *flag = strtok(NULL,  " " );
           
            char message[8000];
            // snprintf(message, sizeof(message), "%s %s %s %s", inst, path, name, flag);
            // printf("mmmmmm %s\n",message);
            handle_create_command(path, name, *flag, message);
            send(client->socket, message, strlen(message), 0);
            break;
            
        }
        // printf("hey\n");
        printf("%s\n", buffer);
        if(strncmp(buffer, " DELETE", 6) == 0){
            printf("hey\n");

            char * inst = strtok(buffer, " ");
            char *path = strtok(NULL, " ");
            printf("delete me");
            char message[8000];
            // snprintf(message, sizeof(message), "%s %s %s %s", inst, path);
             //printf("mmmmmm %s\n",message);
            handle_delete_command(path,message,client->socket);
           // send(client->socket, message, strlen(message), 0);
            break;
            
        }
        
        // Parse request
        if (strncmp(buffer, "STREAM", 6) == 0) {
            // Audio streaming request
            //printf("Received audio streaming request\n");
            char * inst = strtok(buffer, " ");
            char * filename = strtok(NULL, " ");
            handle_audio_request(client->socket, filename);
            break;
        }else if (strncmp(buffer, "READ", 4 )== 0 || strncmp(buffer, "APPEND", 6)==0 || strncmp(buffer, "WRITE", 5)==0 || strncmp(buffer, "INFO",4)==0){
            char buffer2[strlen(buffer) + 1] ;
            strcpy(buffer2, buffer);
            char * inst = strtok(buffer, " ");
            char * filename = strtok(NULL, " ");
            handle_client_request(buffer2, inst,filename,client->socket);
            break;
        } 
        else if (strncmp(buffer, "STOP", 4) == 0) {
            // Client wants to disconnect
            break;
        } 
        else {
            // Unknown request
            char error_msg[] = "Unknown request";
            send(client->socket, error_msg, strlen(error_msg), 0);
            break;
        }
    }

    printf("Client %s:%d disconnected\n", client_ip, ntohs(client->address.sin_port));
    close(client->socket);
    free(client);
    return NULL;
}
void connect_to_client(int storage_port, char* storage_ip) {
    int server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket < 0) {
        printf("socket error (ERROR CODE %d)\n",ERR_SOCK);
        perror("Error creating socket");
        return;
    }

    // Enable address reuse
    int opt = 1;
    if (setsockopt(server_socket, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
        printf("socket error (ERROR CODE %d)\n",ERR_SOCK);
        perror("Error setting socket options");
        close(server_socket);
        return;
    }

    // Set up the storage server address structure
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(storage_port);
    server_addr.sin_addr.s_addr = inet_addr(storage_ip);

    // Bind the socket
    if (bind(server_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        printf("socket error (ERROR CODE %d)\n",ERR_SOCK);
        perror("Error binding socket");
        close(server_socket);
        return;
    }

    // Start listening
    if (listen(server_socket, 10) < 0) {
        printf("socket error (ERROR CODE %d)\n",ERR_SOCK);
        perror("Error listening on socket");
        close(server_socket);
        return;
    }

    printf("Storage server listening on %s:%d\n", storage_ip, storage_port);

    // Accept and handle client connections
    while (1) {
        struct client_info* client = malloc(sizeof(struct client_info));
        socklen_t client_len = sizeof(client->address);

        // Accept new connection
        client->socket = accept(server_socket, (struct sockaddr*)&client->address, &client_len);
        if (client->socket < 0) {
            printf("socket error (ERROR CODE %d)\n",ERR_SOCK);
            perror("Error accepting client connection");
            free(client);
            continue;
        }

        // Create thread to handle this client
        pthread_t thread_id;
        if (pthread_create(&thread_id, NULL, handle_client, client) != 0) {
            perror("Error creating thread");
            close(client->socket);
            free(client);
            continue;
        }

        // Detach thread to allow it to clean up automatically
        pthread_detach(thread_id);
    }

    close(server_socket);
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
int main(int argc, char *argv[]) {
    if (argc < 7) {
        printf("Usage: %s <ns_ip> <ns_port> <client_port> <backup_ip> <backup_port> <accessible_paths...>\n", 
               argv[0]);
        return 1;
    }

    // Retrieve the Naming Server IP, port, and client port from command-line arguments
    char *ns_ip = argv[1];
    int ns_port = atoi(argv[2]);
    int client_port = atoi(argv[3]);
    char *backup_ip = argv[4];
    int backup_port = atoi(argv[5]);
    NS_port = ns_port;
    NS_IP = strdup(ns_ip);
    // Store accessible paths (from command-line arguments)
    const char **paths = (const char **)(argv + 6);
    int num_paths = argc - 6;

    // Example metadata
    const char *metadata = "S";

    // Connect to the Naming Server and send metadata, client port, and accessible paths
    char ip[INET_ADDRSTRLEN];
    find_ss_ip(ip);
     char ss_id[32];
    snprintf(ss_id, sizeof(ss_id), "SS_%s_%d", ip, client_port);

    send_backup_to_server(backup_ip, backup_port, ss_id, paths, num_paths);
    connect_to_ns(ns_ip, ns_port, client_port, metadata, paths, num_paths);
    connect_to_client(client_port, ip);
    return 0;
}
void add_paths_recursive(const char *base_path, char *message, int *path_count) {
    struct dirent *entry;
    struct stat statbuf;
    char full_path[BUFFER_SIZE];
    DIR *dir = opendir(base_path);

    if (!dir) {
        printf("Error opening directory (ERROR CODE %d)\n",ERR_OPENING);
        perror("Unable to open directory");
        return;
    }

    // Add the directory itself to the message
    strcat(message, " ");
    strcat(message, base_path);
    (*path_count)++;

    while ((entry = readdir(dir)) != NULL) {
        // Skip current and parent directory entries
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;

        snprintf(full_path, sizeof(full_path), "%s/%s", base_path, entry->d_name);

        // Check if the entry is a directory or a file
        if (stat(full_path, &statbuf) == 0) {
            if (S_ISDIR(statbuf.st_mode)) {
                // Recurse into the directory
                // printf("hi");
                add_paths_recursive(full_path, message, path_count);
            } else if (S_ISREG(statbuf.st_mode)) {
                // Add the file path to the message
                strcat(message, " ");
                strcat(message, full_path);
                (*path_count)++;
            }
        }
    }

    closedir(dir);
}