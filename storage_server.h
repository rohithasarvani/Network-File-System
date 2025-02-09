#ifndef STORAGE_SERVER_H
#define STORAGE_SERVER_H
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <ifaddrs.h>
#include <sys/stat.h>
#include <dirent.h>
#include <time.h>
#include <sys/select.h>
#include <errno.h>
#include <libgen.h>
// File structure to hold metadata
#define BUFFER_SIZE 4096
struct file_info {
    char type;  // 'F' for file, 'D' for directory
    char path[BUFFER_SIZE];
    char name[256];
    char dest_ip[16];
    int dest_port;
    size_t size;
    char data[BUFFER_SIZE];
};


struct dir_info {
    char name[256];
    char type;  // 'F' for file, 'D' for directory
    int num_entries;
    struct file_info entries[100];  // Array to store directory entries
};


#define CHUNK_SIZE 8192  // 8KB chunks for streaming
#define MAX_FILENAME 256

#define SUCCESS 200
#define FILE_NOT_FOUND 404
#define SERVER_ERROR 500
#define PATH_MAX 4096
#define NS_BUFFER_SIZE 1024
#define NS_COMMAND_SIZE 128
#define ASYNC_THRESHOLD 1024
// Structure to hold NS connection info
struct ns_connection {
    int socket;
    char *ns_ip;
    int ns_port;
    volatile int running;  // Flag to control thread lifecycle
};

#define CHUNK_SIZE 8192  // 8KB chunks for streaming
#define MAX_FILENAME 256

#define MAX_PATH_LENGTH 256
#define BUFFER_SIZE 4096

//to backup other SS (ss-ss)
#define BACKUP_PORT 9010

typedef struct {
    char path[MAX_PATH_LENGTH];
    int is_directory;
} FileInfo;

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

// Add these function declarations
int backup_directory(int sock_fd, const char* base_path, const char* current_path);
void send_backup_to_server(const char* backup_ip, int backup_port, const char* ss_id, 
                          const char** paths, int num_paths);

struct client_info {
    int socket;
    struct sockaddr_in address;
};
// Structure for audio file metadata
struct audio_metadata {
    char filename[MAX_FILENAME];
    long file_size;
    char mime_type[32];
};
struct stream_response {
    int status_code;
    long chunk_size;
    unsigned char data[CHUNK_SIZE];
};
int copy_file_to_ss(const char* source_path, const char* dest_path, const char* dest_ip, int dest_port);
void handle_copy_request(int client_socket, const char *src_path, const char *dest_path, 
                        const char *dest_ip, int dest_port);

// Add these function prototypes to your header file
void handle_ns_commands(int ns_socket);
//int copy_file(const char* source_path, const char* dest_ip, int dest_port, const char* dest_path);
//void send_file(const char* file_path, int dest_socket);
//void receive_file(const char* file_path, int source_socket);
 void handle_create_command(const char *path, const char *name, const char type, char *response);

typedef struct {
    char *filename;
    char *data;
    // char *ns_ip;
    // int ns_port;
    // int client_port;
    // char *client_ip_addr;
    char client_ip[INET_ADDRSTRLEN];
    int client_port;
} WriteTaskArgs;

enum Errorcodes {
    ERR_FILE_NOT_FOUND = 300,
    ERR_FAILED_TO_READ = 301,
    ERR_FAILED_TO_WRITE = 302,
    ERR_FAILED_TO_APPEND = 303,
    ERR_FAILED_TO_DELETE = 304,
    ERR_FAILED_TO_COPY = 305,
    ERR_FAILED_TO_CREATE = 306,
    ERR_FAILED_TO_STREAM_AUDIO = 307,
    ERR_PATH_NOT_FOUND = 308,
    ERR_IS_DIRECTORY = 309,
    ERR_SOCK_SEND = 311,
    ERR_SOCK_CONNECT = 310,
    ERR_SOCK_RECEIVE = 312,
    ERR_SOCK = 313,
    ERR_MAX_SS_REACHED = 314,
    ERR_OPENING = 315,
    ERR_PIPE = 316,
    ERR_INVALID_COMMAND = 317
};
#endif