#ifndef NAMING_SERVER_H
#define NAMING_SERVER_H
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include<stdbool.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <sys/socket.h>
#include <pthread.h>
// #include <time.h>
#include <ifaddrs.h>
#include <sys/stat.h>
#include <dirent.h>
#include <time.h>
#include <signal.h>
#define MAX_STORAGE_SERVERS 100   // Maximum number of storage servers
#define BUFFER_SIZE 4096          // Size of the buffer for communication
#define NS_PORT 8099           // Port for Naming Server
// #define MAX_PATHS 10          // Maximum number of accessible paths
#define INET_ADDRSTRLEN 16        // Length of the string for IP address- IPv4

#define RS1_PORT 9000
#define RS2_PORT 9001

#define TABLE_SIZE 101  // Prime number for better distribution
#define MAX_PATHS 256   // Example maximum paths

#define MAX_QUEUE_SIZE 100
#define ACK_PREFIX 1000  // Starting point for ACK numbers

// Add these to naming_server.h
#define MAX_SS_CONNECTIONS 10
// Enhanced SS connection handling structure
typedef struct {
    int socket;
    char ip_address[INET_ADDRSTRLEN];
    int port;
    int client_port;
    bool is_active;
    pthread_t thread;
    time_t last_heartbeat;
    int pending_ops;  // Track pending operations
    pthread_mutex_t op_lock;  // Lock for operations
} SSConnection;

// Command response structure
typedef struct {
    int status;
    char message[BUFFER_SIZE];
} CommandResponse;


typedef struct {
    SSConnection connections[MAX_SS_CONNECTIONS];
    int count;
    pthread_mutex_t lock;
} SSConnectionManager;


typedef struct {
    int client_socket;
    char client_ip[INET_ADDRSTRLEN];
    int client_port;
    int ack_number;
    time_t timestamp;
    char request[BUFFER_SIZE];
    // char file_path[256];
} ClientRequest;

typedef struct {
    ClientRequest requests[MAX_QUEUE_SIZE];
    int front;
    int rear;
    int size;
    pthread_mutex_t mutex;
    pthread_cond_t not_empty;
    pthread_cond_t not_full;
} RequestQueue;



// // Struct to store metadata for each storage server

typedef struct {
    char path[256];      // File or folder path name
    bool is_occupied;    // Flag to indicate if slot is occupied
    bool is_deleted;     // Flag to indicate if slot is a tombstone
} HashEntry;

typedef struct {
    char ip_address[INET_ADDRSTRLEN];  // Storage server's IP address
    int port;                          // Storage server's port for NM connection
    int client_port;                   // Storage server's port for client connection
    char metadata[256];                // Additional metadata
    // char accessible_paths[MAX_PATHS][256];  // List of accessible paths
    HashEntry accessible_paths[TABLE_SIZE];  // Hash table for accessible paths
    int num_paths;                     // Number of accessible paths
    // int is_occupied;  // Flag to indicate if the slot is occupied
} StorageServer;



// Function declarations
void find_ip(char *ip);
void register_storage_server(const char *ip_address, int port, int client_port, const char *metadata, const char *paths[], int num_paths);
void start_naming_server(int port);
void handle_storage_server(int client_socket, struct sockaddr_in *client_addr);
void send_metadata_to_replica(const char *metadata, const char *replica_ip, int replica_port);
void init_storage_servers();

void handle_client(int client_socket, const char *client_ip, int port);
StorageServer* get_ss_ipandport(char *filepath);
StorageServer insert_path(StorageServer server, const char *path) ;
bool delete_path(StorageServer *server, const char *path) ;
int search_path(StorageServer server, const char *path) ;


int find_ss_connection(const char* ip, int port);

void init_storage_servers();
void init_logging();
void close_logging();
void get_timestamp(char* timestamp_str, size_t size) ;
void log_message(const char* level, const char* message);
// void log_client_request(const char* client_ip, int client_port, const char* request, int ack_number);
void log_client_request(const char* client_ip, int client_port, int client_socket_fd, const char* request, int ack_number) ;
void log_storage_server_registration(const char* ip_address, int port, int client_port, const char* metadata, int num_paths);
int find_client_info_from_log(const char *filename, char *client_ip, int *client_port, int* client_sock_fd);
void notify_client_of_completion(const char *filename) ;
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

#define MAX_PENDING_WRITES 100



#endif