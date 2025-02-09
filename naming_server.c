#include "naming_server.h"
#include "cache.h"
// #include "storage_server.h"

// Global cache instance
LRUCache *cache;
SSConnectionManager ss_manager;

// Initialize the SS connection manager
void init_ss_connection_manager()
{
    ss_manager.count = 0;
    pthread_mutex_init(&ss_manager.lock, NULL);
    for (int i = 0; i < MAX_SS_CONNECTIONS; i++)
    {
        ss_manager.connections[i].is_active = false;
    }
}
// Add a new SS connection
int add_ss_connection(int socket, const char *ip, int port, int client_port)
{
    pthread_mutex_lock(&ss_manager.lock);

    int index = -1;
    for (int i = 0; i < MAX_SS_CONNECTIONS; i++)
    {
        if (!ss_manager.connections[i].is_active)
        {
            index = i;
            break;
        }
    }

    if (index != -1)
    {
        ss_manager.connections[index].socket = socket;
        strncpy(ss_manager.connections[index].ip_address, ip, INET_ADDRSTRLEN);
        ss_manager.connections[index].port = port;
        ss_manager.connections[index].client_port = client_port;
        ss_manager.connections[index].is_active = true;
        ss_manager.count++;
    }

    pthread_mutex_unlock(&ss_manager.lock);
    return index;
}

// Remove an SS connection
void remove_ss_connection(int index)
{
    pthread_mutex_lock(&ss_manager.lock);

    if (index >= 0 && index < MAX_SS_CONNECTIONS && ss_manager.connections[index].is_active)
    {
        close(ss_manager.connections[index].socket);
        ss_manager.connections[index].is_active = false;
        ss_manager.count--;
    }

    pthread_mutex_unlock(&ss_manager.lock);
}

// Thread function to handle individual SS connection
void *handle_ss_connection(void *arg)
{
    int index = *((int *)arg);
    free(arg);

    SSConnection *conn = &ss_manager.connections[index];
    char buffer[BUFFER_SIZE];
    return NULL;
}

// Initialize the LRU cache
LRUCache *init_lru_cache(int capacity)
{
    LRUCache *cache = (LRUCache *)malloc(sizeof(LRUCache));
    cache->capacity = capacity;
    cache->size = 0;
    cache->head = NULL;
    cache->tail = NULL;

    // Initialize hash table
    memset(cache->hash, 0, sizeof(CacheNode *) * CACHE_SIZE);

    // Initialize mutex
    pthread_mutex_init(&cache->lock, NULL);

    return cache;
}

// Hash function for paths
unsigned int cache_hash(const char *path)
{
    unsigned int hash = 0;
    while (*path)
    {
        hash = (hash * 31) + *path++;
    }
    return hash % CACHE_SIZE;
}

// Move node to front (most recently used)
void move_to_front(LRUCache *cache, CacheNode *node)
{
    if (node == cache->head)
    {
        return; // Already at front
    }

    // Remove from current position
    if (node->prev)
    {
        node->prev->next = node->next;
    }
    if (node->next)
    {
        node->next->prev = node->prev;
    }
    if (node == cache->tail)
    {
        cache->tail = node->prev;
    }

    // Move to front
    node->next = cache->head;
    node->prev = NULL;
    if (cache->head)
    {
        cache->head->prev = node;
    }
    cache->head = node;
    if (!cache->tail)
    {
        cache->tail = node;
    }
}

// Create new cache node
CacheNode *create_node(const char *path, const char *ss_ip, int ss_port)
{
    CacheNode *node = (CacheNode *)malloc(sizeof(CacheNode));
    strncpy(node->path, path, sizeof(node->path) - 1);
    strncpy(node->ss_ip, ss_ip, sizeof(node->ss_ip) - 1);
    node->ss_port = ss_port;
    node->prev = NULL;
    node->next = NULL;
    return node;
}

// Add or update entry in cache
void cache_put(LRUCache *cache, const char *path, const char *ss_ip, int ss_port)
{
    pthread_mutex_lock(&cache->lock);

    unsigned int hash_key = cache_hash(path);
    CacheNode *existing = cache->hash[hash_key];

    if (existing)
    {
        // Update existing entry
        strncpy(existing->ss_ip, ss_ip, sizeof(existing->ss_ip) - 1);
        existing->ss_port = ss_port;
        move_to_front(cache, existing);
    }
    else
    {
        // Create new entry
        CacheNode *new_node = create_node(path, ss_ip, ss_port);

        // If cache is full, remove least recently used
        if (cache->size >= cache->capacity)
        {
            CacheNode *lru = cache->tail;
            unsigned int lru_hash = cache_hash(lru->path);
            cache->hash[lru_hash] = NULL;

            cache->tail = lru->prev;
            if (cache->tail)
            {
                cache->tail->next = NULL;
            }
            free(lru);
            cache->size--;
        }

        // Add new node to front
        new_node->next = cache->head;
        if (cache->head)
        {
            cache->head->prev = new_node;
        }
        cache->head = new_node;
        if (!cache->tail)
        {
            cache->tail = new_node;
        }

        cache->hash[hash_key] = new_node;
        cache->size++;
    }

    pthread_mutex_unlock(&cache->lock);
}

// Get entry from cache
bool cache_get(LRUCache *cache, const char *path, char *ss_ip, int *ss_port)
{
    pthread_mutex_lock(&cache->lock);

    unsigned int hash_key = cache_hash(path);
    CacheNode *node = cache->hash[hash_key];

    if (node && strcmp(node->path, path) == 0)
    {
        // Cache hit
        strncpy(ss_ip, node->ss_ip, INET_ADDRSTRLEN);
        *ss_port = node->ss_port;
        move_to_front(cache, node);
        pthread_mutex_unlock(&cache->lock);
        return true;
    }

    pthread_mutex_unlock(&cache->lock);
    return false;
}

// Clean up cache
void free_lru_cache(LRUCache *cache)
{
    pthread_mutex_lock(&cache->lock);

    CacheNode *current = cache->head;
    while (current)
    {
        CacheNode *next = current->next;
        free(current);
        current = next;
    }

    pthread_mutex_unlock(&cache->lock);
    pthread_mutex_destroy(&cache->lock);
    free(cache);
}

// Print cache contents (for debugging)
void print_cache_contents(LRUCache *cache)
{
    pthread_mutex_lock(&cache->lock);

    printf("\nCache Contents (Most Recent First):\n");
    CacheNode *current = cache->head;
    while (current)
    {
        printf("Path: %s -> SS: %s:%d\n",
               current->path, current->ss_ip, current->ss_port);
        current = current->next;
    }
    printf("Cache size: %d/%d\n", cache->size, cache->capacity);

    pthread_mutex_unlock(&cache->lock);
}
// #define TABLE_SIZE 101  // Prime number for better distribution
// #define MAX_PATHS 256   // Example maximum paths

// typedef struct {
//     char path[256];      // File or folder path name
//     bool is_occupied;    // Flag to indicate if slot is occupied
//     bool is_deleted;     // Flag to indicate if slot is a tombstone
// } HashEntry;

// typedef struct {
//     char ip_address[INET_ADDRSTRLEN];  // Storage server's IP address
//     int port;                          // Storage server's port for NM connection
//     int client_port;                   // Storage server's port for client connection
//     char metadata[256];                // Additional metadata
//     // char accessible_paths[MAX_PATHS][256];  // List of accessible paths
//     HashEntry accessible_paths[TABLE_SIZE];  // Hash table for accessible paths
//     int num_paths;                     // Number of accessible paths
//     // int is_occupied;  // Flag to indicate if the slot is occupied
// } StorageServer;

// Array to store registered storage servers
RequestQueue request_queue;
int next_ack_number = ACK_PREFIX;

// Initialize the request queue
void init_request_queue()
{
    request_queue.front = 0;
    request_queue.rear = -1;
    request_queue.size = 0;
    pthread_mutex_init(&request_queue.mutex, NULL);
    pthread_cond_init(&request_queue.not_empty, NULL);
    pthread_cond_init(&request_queue.not_full, NULL);
}

// Add request to queue
int enqueue_request(ClientRequest request)
{
    pthread_mutex_lock(&request_queue.mutex);

    while (request_queue.size >= MAX_QUEUE_SIZE)
    {
        pthread_cond_wait(&request_queue.not_full, &request_queue.mutex);
    }

    request_queue.rear = (request_queue.rear + 1) % MAX_QUEUE_SIZE;
    request_queue.requests[request_queue.rear] = request;
    request_queue.size++;

    pthread_cond_signal(&request_queue.not_empty);
    pthread_mutex_unlock(&request_queue.mutex);

    return request.ack_number;
}

// Get next request from queue
ClientRequest dequeue_request()
{
    pthread_mutex_lock(&request_queue.mutex);

    while (request_queue.size == 0)
    {
        pthread_cond_wait(&request_queue.not_empty, &request_queue.mutex);
    }

    ClientRequest request = request_queue.requests[request_queue.front];
    request_queue.front = (request_queue.front + 1) % MAX_QUEUE_SIZE;
    request_queue.size--;

    pthread_cond_signal(&request_queue.not_full);
    pthread_mutex_unlock(&request_queue.mutex);

    return request;
}

// Worker thread function to process requests
void *process_requests(void *arg)
{
    while (1)
    {
        ClientRequest request = dequeue_request();
        printf("Processing request with ACK %d from %s:%d\n",
               request.ack_number, request.client_ip, request.client_port);

        // Process the request using existing handle_client function
        handle_client(request.client_socket, request.client_ip, request.client_port);
    }
    return NULL;
}

StorageServer storage_servers[MAX_STORAGE_SERVERS];
int server_count = 0;

unsigned int hash(const char *str)
{
    unsigned int hash = 0;
    while (*str)
    {
        hash = (hash * 31) + *str++; // 31 is chosen as a multiplier for distribution
    }
    return hash % TABLE_SIZE;
}

// function to find storage server in which path is present
StorageServer *get_ss_ipandport(char *filepath)
{
    char cached_ip[INET_ADDRSTRLEN];
    int cached_port;

    // Try to get from cache first
    if (cache_get(cache, filepath, cached_ip, &cached_port))
    {
        printf("Cache hit for path: %s\n", filepath);
        // Create temporary StorageServer struct for cache hit
        static StorageServer cached_server;
        strncpy(cached_server.ip_address, cached_ip, INET_ADDRSTRLEN);
        cached_server.client_port = cached_port;
        return &cached_server;
    }

    printf("Cache miss for path: %s\n", filepath);

    // StorageServer S ;
    // Original storage server lookup
    for (int i = 0; i < server_count; i++)
    {
        int res = search_path(storage_servers[i], filepath);
        if (res == 1)
        {
            // Add to cache before returning
            cache_put(cache, filepath,
                      storage_servers[i].ip_address,
                      storage_servers[i].client_port);
            printf("Added to cache: %s -> %s:%d\n",
                   filepath, storage_servers[i].ip_address,
                   storage_servers[i].client_port);
            return &storage_servers[i];
        }
    }
    return NULL;
}

// Insert a file or folder path into the hash table with quadratic probing
StorageServer insert_path(StorageServer server, const char *path)
{
    unsigned int index = hash(path);
    unsigned int i = 1;

    while (server.accessible_paths[index].is_occupied && !server.accessible_paths[index].is_deleted)
    {
        index = (index + i * i) % TABLE_SIZE;
        i++;
        if (i > TABLE_SIZE)
            return server; // Table is full, return unmodified
    }

    strcpy(server.accessible_paths[index].path, path);
    server.accessible_paths[index].is_occupied = true;
    server.accessible_paths[index].is_deleted = false;
    server.num_paths++;
    return server;
}

// Delete a file or folder path from the hash table with quadratic probing
bool delete_path(StorageServer *server, const char *path)
{
    unsigned int index = hash(path);
    unsigned int i = 1;

    while (server->accessible_paths[index].is_occupied)
    {
        if (!server->accessible_paths[index].is_deleted &&
            strcmp(server->accessible_paths[index].path, path) == 0)
        {
            server->accessible_paths[index].is_deleted = true;
            server->num_paths--;
            return true; // Path deleted
        }
        index = (index + i * i) % TABLE_SIZE;
        i++;
        if (i > TABLE_SIZE)
            return false; // Path not found
    }
    return false; // Path not found
}

// Search for a file or folder path in the hash table with quadratic probing
int search_path(StorageServer server, const char *path)
{
    unsigned int index = hash(path);
    unsigned int i = 1;

    while (server.accessible_paths[index].is_occupied)
    {
        if (!server.accessible_paths[index].is_deleted &&
            strcmp(server.accessible_paths[index].path, path) == 0)
        {
            return 1; // Path found
        }
        index = (index + i * i) % TABLE_SIZE;
        i++;
        if (i > TABLE_SIZE)
            return 0; // Path not found
    }
    return 0; // Path not found
}

// Function to register a storage server in the array
void register_storage_server(const char *ip_address, int port, int client_port, const char *metadata, const char *paths[], int num_paths)
{
    if (server_count < MAX_STORAGE_SERVERS)
    {
        // Set IP address, ports, metadata, and initialize paths
        strcpy(storage_servers[server_count].ip_address, ip_address);
        storage_servers[server_count].port = port;
        storage_servers[server_count].client_port = client_port;
        strcpy(storage_servers[server_count].metadata, metadata);
        storage_servers[server_count].num_paths = 0; // Start with 0 and increment as paths are added

        // Insert each path into the hash table within the struct
        for (int i = 0; i < num_paths; i++)
        {
            storage_servers[server_count] = insert_path(storage_servers[server_count], paths[i]);
        }

        // Increment server count after registration
        server_count++;
        printf("Registered storage server %s:%d (Client Port: %d) with metadata: %s and %d accessible paths.\n",
               ip_address, port, client_port, metadata, storage_servers[server_count - 1].num_paths);
    }
    else
    {
        printf("Error: Maximum number of storage servers reached.\n");
    }
}

// Function to start the naming server
void start_naming_server(int port)
{
    int server_socket;
    struct sockaddr_in server_addr, client_addr;
    socklen_t client_addr_len = sizeof(client_addr);

    // Create socket for the naming server
    server_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (server_socket < 0){
        printf("socket error (ERROR CODE %d)\n",ERR_SOCK);
        perror("Error creating socket");
        exit(EXIT_FAILURE);
    }

    // Configure server address structure
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY; // Bind to any IP address
    server_addr.sin_port = htons(port);       // Set the port

    // Bind the socket to the specified IP and port
    if (bind(server_socket, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        printf("socket error (ERROR CODE %d)\n",ERR_SOCK);
        perror("Error binding socket");
        close(server_socket);
        exit(EXIT_FAILURE);
    }

    // Start listening for incoming connections
    if (listen(server_socket, 10) < 0)
    {
        printf("socket error (ERROR CODE %d)\n",ERR_SOCK);
        perror("Error listening on socket");
        close(server_socket);
        exit(EXIT_FAILURE);
    }
    printf("Naming server started, waiting for storage servers on port %d...\n", port);

    cache = init_lru_cache(CACHE_SIZE);
    printf("Initialized LRU cache with capacity %d\n", CACHE_SIZE);
    // for storing trail;
    //    register_storage_server("192.168.1.10", 9095, "Server 6");
    // register_storage_server("192.168.1.11", 8084, "Server 7");
    // register_storage_server("192.168.1.5", 9094, "Server 8");

    // Main loop to accept and handle connections from storage servers
    while (1)
    {
        // printf("hhhh");
        // Accept connection from a storage server
        int client_socket = accept(server_socket, (struct sockaddr *)&client_addr, &client_addr_len);
        if (client_socket < 0)
        {
            printf("socket error (ERROR CODE %d)\n",ERR_SOCK);
            perror("Error accepting connection");
            continue;
        }

        // Handle the connected storage server
        handle_storage_server(client_socket, &client_addr);
    }

    // Close the naming server socket (not reachable in this example)
    close(server_socket);
}

// Function to replicate metadata to replica servers
void replicate_metadata(const char *metadata)
{
    // Replicate to Replica Server 1
    send_metadata_to_replica(metadata, "127.0.0.1", RS1_PORT);

    // Replicate to Replica Server 2
    send_metadata_to_replica(metadata, "127.0.0.1", RS2_PORT);
}

// Function to send metadata to a specific replica server
void send_metadata_to_replica(const char *metadata, const char *replica_ip, int replica_port)
{
    int socket_fd;
    struct sockaddr_in replica_addr;

    // Create socket
    socket_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (socket_fd < 0)
    {
        printf("socket error (ERROR CODE %d)\n",ERR_SOCK);
        perror("Error creating socket to Replica Server");
        return;
    }

    // Configure replica address
    replica_addr.sin_family = AF_INET;
    replica_addr.sin_port = htons(replica_port);
    inet_pton(AF_INET, replica_ip, &replica_addr.sin_addr);

    // Connect to replica server
    if (connect(socket_fd, (struct sockaddr *)&replica_addr, sizeof(replica_addr)) < 0)
    {
        printf("socket connect error (ERROR CODE %d)\n",ERR_SOCK_CONNECT);
        perror("Error connecting to Replica Server");
        close(socket_fd);
        return;
    }

    // Send metadata to replica
    send(socket_fd, metadata, strlen(metadata), 0);
    printf("Replicated metadata to Replica Server (%s:%d): %s\n", replica_ip, replica_port, metadata);

    // Close the connection
    close(socket_fd);
}
// Returns 1 on success, 0 on failure
int connect_and_send_to_ss(char* ip, int port, char* message) {
    int sock = 0;
    struct sockaddr_in serv_addr;
    char buffer[BUFFER_SIZE] = {0};
    int status = 0;

    // Create socket
    if ((sock = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
        printf("socket error (ERROR CODE %d)\n",ERR_SOCK);
        // printf("Socket creation error\n");
        return 0;
    }

    serv_addr.sin_family = AF_INET;
    serv_addr.sin_port = htons(port);

    // Convert IPv4 and IPv6 addresses from text to binary form
    if (inet_pton(AF_INET, ip, &serv_addr.sin_addr) <= 0) {
        printf("Invalid address/ Address not supported\n");
        close(sock);
        return 0;
    }

    // Connect to the storage server
    if (connect(sock, (struct sockaddr *)&serv_addr, sizeof(serv_addr)) < 0) {
        // printf("Connection Failed\n");
        printf("socket connection error (ERROR CODE %d)\n",ERR_SOCK_CONNECT);
        close(sock);
        return 0;
    }

    // Send the message
    if (send(sock, message, strlen(message), 0) < 0) {
        // printf("Send failed\n");
        printf("socket send error (ERROR CODE %d)\n",ERR_SOCK_SEND);
        close(sock);
        return 0;
    }

    // Wait for response
    int bytes_read = read(sock, buffer, BUFFER_SIZE);
    if (bytes_read < 0) {
        printf("Read failed\n");
        status = 0;
    } else {
        buffer[bytes_read] = '\0';
        printf("Response from SS: %s\n", buffer);
        
        // Check if response indicates success
        if (strstr(buffer, "Success") != NULL) {
            status = 1;
        }
    }

    // Close the connection
    close(sock);
    return status;
}
void notify_client_of_completion(const char *filename) {
    char client_ip[INET_ADDRSTRLEN];
    int client_port;int client_sock_fd;

    // Search the log file for the client info
    if (find_client_info_from_log(filename, client_ip, &client_port, &client_sock_fd) == 0) {
        printf("Found client for async write completion: IP: %s, Port: %d client_sock_fd: %d\n", client_ip, client_port,client_sock_fd);

        // Now send acknowledgment to the client
        // int client_sock = socket(AF_INET, SOCK_STREAM, 0);
        // struct sockaddr_in client_addr;
        // client_addr.sin_family = AF_INET;
        // client_addr.sin_port = htons(client_port);
        // inet_pton(AF_INET, client_ip, &client_addr.sin_addr);

        // if (connect(client_sock, (struct sockaddr*)&client_addr, sizeof(client_addr)) == 0) {
            char ack_message[100];
            snprintf(ack_message, sizeof(ack_message), "Async write completed for file: %s", filename);
            printf("ACK %s\n",ack_message);
            send(client_sock_fd, ack_message, strlen(ack_message), 0);
            printf("Acknowledgment sent to client: %s:%d\n", client_ip, client_port);
        // } else { 
        //     perror("Error connecting to client");
        // }

        // close(client_sock_fd);
    } else {
        printf("Client info not found in log for file: %s\n", filename);
    }
}
// Function to handle connections from storage servers
// Function to handle connections from storage servers
void handle_storage_server(int client_socket, struct sockaddr_in *client_addr)
{
    char buffer[BUFFER_SIZE];
    char metadata[256];
    char paths[MAX_PATHS][256];
    int port, client_port;
    int num_paths = 0;

    // Get client IP and port
    char client_ip[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &(client_addr->sin_addr), client_ip, INET_ADDRSTRLEN);
    port = ntohs(client_addr->sin_port);

    // Receive message (metadata, client port, and accessible paths)
    memset(buffer, 0, sizeof(buffer));
    recv(client_socket, buffer, sizeof(buffer) - 1, 0);
    if (strncmp(buffer, "ASYNCWRITE_COMPLETE", 18) == 0) {
        char filename[256];
        char client_ip[INET_ADDRSTRLEN];
        int clientt_portt;
        char result[256];
        
        // Parse the completion message
        sscanf(buffer, "ASYNCWRITE_COMPLETE %s %s %d %[^\n]", 
               filename, client_ip, &clientt_portt, result);

        printf("async write done %s\n",buffer);
        printf("file : %s client ip: %s client port: %d\n",filename,client_ip,clientt_portt);
        
        // Forward the completion message to the client
        // forward_write_completion_to_client(client_ip, clientt_portt, filename, result);
        notify_client_of_completion(filename);
        
        // Send acknowledgment back to storage server
        // const char *ack = "ASYNC WRITE Completion notification received";
        // send(client_socket, ack, strlen(ack), 0);
        close(client_socket);
        return;
    }
    else if (strncmp(buffer, "Metadata", 8) != 0)
    {
        ClientRequest new_request;
        new_request.client_socket = client_socket;
        strcpy(new_request.client_ip, client_ip);
        new_request.client_port = port;
        new_request.ack_number = next_ack_number++;
        new_request.timestamp = time(NULL);
        strcpy(new_request.request, buffer);

        // Send ACK number to client
        char ack_message[50];
        sprintf(ack_message, "ACK:%d", new_request.ack_number);
        send(client_socket, ack_message, strlen(ack_message), 0);

        // Add request to queue
        enqueue_request(new_request);
        // handle_client(client_socket, client_ip, port);
        return;
    }

    // Parse the received message (Assume the format: "Metadata: <metadata>, Client Port: <client_port>, Paths: <path1> <path2> ...")
    sscanf(buffer, "Metadata: %[^,], Client Port: %d", metadata, &client_port);

    // Extract paths
    char *paths_str = strstr(buffer, "Paths:") + 6;
    char *token = strtok(paths_str, " ");
    while (token && num_paths < MAX_PATHS)
    {
        strcpy(paths[num_paths], token);
        token = strtok(NULL, " ");
        num_paths++;
    }

    printf("Received metadata: %s, Client Port: %d, %d accessible paths from storage server %s\n", metadata, client_port, num_paths, client_ip);
    log_storage_server_registration(client_ip, port, client_port, metadata, num_paths);

    // Register the storage server
    const char *path_list[MAX_PATHS];
    for (int i = 0; i < num_paths; i++)
    {
        path_list[i] = paths[i];
    }
    register_storage_server(client_ip, port, client_port, metadata, path_list, num_paths);
    // Add to connection manager and start thread
    int conn_index = add_ss_connection(client_socket, client_ip, port, client_port);
    printf("Added storage server connection at index %d\n", conn_index);
    if (conn_index != -1)
    {
        int *thread_arg = malloc(sizeof(int));
        *thread_arg = conn_index;
        pthread_create(&ss_manager.connections[conn_index].thread,
                       NULL,
                       handle_ss_connection,
                       thread_arg);

        // Send acknowledgment
        const char *ack_message = "Storage server registered successfully";
        send(client_socket, ack_message, strlen(ack_message), 0);
    }
    else
    {
        const char *error_message = "Maximum storage servers reached";
        send(client_socket, error_message, strlen(error_message), 0);
        close(client_socket);
    }
    // Send acknowledgment to the storage server
    const char *ack_message = "Storage server registered successfully";
    send(client_socket, ack_message, strlen(ack_message), 0);

    // Close connection
    // close(client_socket);

}

void handle_client(int client_socket, const char *client_ip, int port)
{
    char buffer[BUFFER_SIZE];
    int bytes_received;


    while ((bytes_received = recv(client_socket, buffer, sizeof(buffer) - 1, 0)) > 0)
    {
        buffer[bytes_received] = '\0'; // Null-terminate the received string
        char command2[8192];
        strcpy(command2, buffer);
        //printf("Command2 - %s\n", command2);

        printf("Received command from client %s:%d: %s\n", client_ip, port, buffer);
        log_client_request(client_ip, port,client_socket, buffer, next_ack_number - 1);
        char *inst = strtok(buffer, " ");
        char *path = strtok(NULL, " ");
        char * path2;
        if(strncmp(inst, "COPY", 4) == 0){
            path2 = strtok(NULL," ");
            StorageServer * retrieved_ss_source = get_ss_ipandport(path);
            StorageServer * retrieved_ss_destination = get_ss_ipandport(path2);
            if(!retrieved_ss_source || !retrieved_ss_destination){
                printf("Path not found\n");
                char mssg[BUFFER_SIZE];
                strcpy(mssg, "Path not found");
                send(client_socket, mssg, strlen(mssg), 0);
                continue;
            }
            
            char *source = retrieved_ss_source->ip_address;
            int source_port = retrieved_ss_source->client_port;
            int destination_port = retrieved_ss_destination->client_port;
            char *destination = retrieved_ss_destination->ip_address;
        char *inst2 = strtok(command2, " ");
        path = strtok(NULL, " ");
        path2 = strtok(NULL, " ");
            memset(buffer, 0, sizeof(buffer));
            sprintf(buffer, "COPY %s %s %s %d", path, path2, destination, destination_port);
            int success =connect_and_send_to_ss(source, source_port, buffer);
            if(success){
                send(client_socket, "Successful Copy", strlen("Success"), 0);
            }
            continue;
        }
        if(strncmp(inst,"DELETE", 6) == 0){
                   

            StorageServer * retrieved_ss_source = get_ss_ipandport(path);
            if(!retrieved_ss_source){
                // printf("Path not found\n");
                printf("Path not found (ERROR CODE %d)\n",ERR_PATH_NOT_FOUND);
                char mssg[BUFFER_SIZE];
                strcpy(mssg, "Path not found");
                send(client_socket, mssg, strlen(mssg), 0);
                continue;
            }
            char *source = retrieved_ss_source->ip_address;
            int source_port = retrieved_ss_source->client_port;
            char *inst2 = strtok(command2, " ");
        path = strtok(NULL, " ");
        
            memset(buffer, 0, sizeof(buffer));
            sprintf(buffer, " DELETE %s %s %d", path, source, source_port);
            int success =connect_and_send_to_ss(source, source_port, buffer);
            if(success){
                send(client_socket, "Successful Create", strlen("Successful Create"), 0);
            }
            // if(recv(client_socket, buffer, sizeof(buffer), 0) > 0){
            //     printf("Buffer: %s\n", buffer);
            // }
            continue;
            }
            if(strncmp(inst,"CREATE",6) == 0){
                    char* name = strtok(NULL," ");
            char* flag = strtok(NULL, " ");
            StorageServer * retrieved_ss_source = get_ss_ipandport(path);
            if(!retrieved_ss_source){
                // printf("Path not found\n");
                printf("Path not found (ERROR CODE %d)\n",ERR_PATH_NOT_FOUND);
                char mssg[BUFFER_SIZE];
                strcpy(mssg, "Path not found");
                send(client_socket, mssg, strlen(mssg), 0);
                continue;
            }
            char *source = retrieved_ss_source->ip_address;
            int source_port = retrieved_ss_source->client_port;
            char *inst2 = strtok(command2, " ");
        path = strtok(NULL, " ");
        name = strtok(NULL, " ");
        flag = strtok(NULL, " ");
            memset(buffer, 0, sizeof(buffer));
            char full_name[512]; // Make sure this is large enough to hold the full path
            if (path != NULL && name != NULL) {
                snprintf(full_name, sizeof(full_name), "%s/%s", path, name);
                printf("Full name: %s\n", full_name);
            } 
            // else {
                // printf("Error: Path or Name is NULL\n");
            // }
            if (retrieved_ss_source != NULL) {
                StorageServer ss = insert_path(*retrieved_ss_source, full_name);
                *retrieved_ss_source = ss; // Copy the modified value back to the original
            }
            sprintf(buffer, "CREATE %s %s %s %s %d", path, name, flag, source, source_port);
            int success =connect_and_send_to_ss(source, source_port, buffer);
            if(success){
                send(client_socket, "Successful Create", strlen("Successful Create"), 0);
            }
            // if(recv(client_socket, buffer, sizeof(buffer), 0) > 0){
            //     printf("Buffer: %s\n", buffer);
            // }
            
            continue;
            }
       
        if (!path)
        {
            printf("Path not given.\n");
            char *message = "Path not given.";
            send(client_socket, message, strlen(message), 0);
            continue;
            // return;
        }
        printf("Instruction: %s, Path: %s.\n", inst, path);
        bool copy_f = 0;
        StorageServer *retrieved_ss_source;
        StorageServer *retrieved_ss_destination;
        // Acknowledge based on the command type
        char *source;
        char *destination;
            StorageServer *retrieved_ss = get_ss_ipandport(path);
            char *retrieved_ss_ip = retrieved_ss->ip_address;
            if (!retrieved_ss_ip)
            {
                // printf("Path not found\n");
                printf("Path not found (ERROR CODE %d)\n",ERR_PATH_NOT_FOUND);
                char mssg[BUFFER_SIZE];
                strcpy(mssg, "Path not found");
                send(client_socket, mssg, strlen(mssg), 0);
                // return;
                continue;
                // return;
            }
            int retrieved_ss_port = retrieved_ss->client_port;
            printf("Retrieved storage server IP: %s, Port: %d\n", retrieved_ss_ip, retrieved_ss_port);
            char message[BUFFER_SIZE];
            sprintf(message, "Storage Server IP: %s, Port: %d", retrieved_ss_ip, retrieved_ss_port);
            send(client_socket, message, strlen(message), 0);
    
        
    }
    if (bytes_received == 0)
    {
        printf("Client %s:%d disconnected.\n", client_ip, port);
    }
    else if (bytes_received < 0)
    {
        perror("Error receiving data");
    }

    close(client_socket); // Close the connection
}

void find_ip(char *ip)
{
    struct ifaddrs *ifaddr, *ifa;

    // Retrieve the linked list of network interfaces
    if (getifaddrs(&ifaddr) == -1)
    {
        perror("getifaddrs");
        exit(EXIT_FAILURE);
    }

    // Loop through the list of interfaces
    for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next)
    {
        if (ifa->ifa_addr == NULL)
        {
            continue;
        }

        // Check for IPv4 addresses only and ignore loopback addresses
        if (ifa->ifa_addr->sa_family == AF_INET)
        {
            // Get the address in a human-readable form
            void *addr = &((struct sockaddr_in *)ifa->ifa_addr)->sin_addr;
            inet_ntop(AF_INET, addr, ip, INET_ADDRSTRLEN);

            // Skip loopback addresses (starting with "127.")
            if (strncmp(ip, "127.", 4) != 0)
            {
                break; // Stop after finding the first non-loopback IPv4 address
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

// Add these defines at the top of naming_server.c
#define LOG_FILE "naming_server_log.txt"
#define MAX_LOG_LENGTH 512

// Add these global variables
FILE* log_file = NULL;
pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;

// Initialize logging system
void init_logging() {
    log_file = fopen(LOG_FILE, "a");
    if (log_file == NULL) {
        printf("Error opening log file (ERROR CODE %d)\n",ERR_OPENING);
        perror("Error opening log file");
        exit(EXIT_FAILURE);
    }
    // Write startup message
    log_message("INFO", "Naming Server started");
}

// Close logging system
void close_logging() {
    if (log_file) {
        log_message("INFO", "Naming Server shutting down");
        fclose(log_file);
    }
    pthread_mutex_destroy(&log_mutex);
}

// Function to get current timestamp as string
void get_timestamp(char* timestamp_str, size_t size) {
    time_t now = time(NULL);
    struct tm* tm_info = localtime(&now);
    strftime(timestamp_str, size, "%Y-%m-%d %H:%M:%S", tm_info);
}

// Main logging function
void log_message(const char* level, const char* message) {
    char timestamp[26];
    get_timestamp(timestamp, sizeof(timestamp));
    
    pthread_mutex_lock(&log_mutex);
    fprintf(log_file, "[%s] [%s] %s\n", timestamp, level, message);
    fflush(log_file);
    pthread_mutex_unlock(&log_mutex);
    
    // Also print to console
    printf("[%s] [%s] %s\n", timestamp, level, message);
}

// Function to log client requests
void log_client_request(const char* client_ip, int client_port, int client_socket_fd, const char* request, int ack_number) {
    char log_buffer[MAX_LOG_LENGTH];
    snprintf(log_buffer, sizeof(log_buffer), 
             "Client Request - IP: %s, Port: %d, Socket FD: %d, ACK: %d, Request: %s",
             client_ip, client_port, client_socket_fd, ack_number, request);
    log_message("REQUEST", log_buffer);
}


// Function to log storage server registration
void log_storage_server_registration(const char* ip_address, int port, int client_port, const char* metadata, int num_paths) {
    char log_buffer[MAX_LOG_LENGTH];
    snprintf(log_buffer, sizeof(log_buffer), 
             "Storage Server Registration - IP: %s, Port: %d, Client Port: %d, Metadata: %s, Paths: %d",
             ip_address, port, client_port, metadata, num_paths);
    log_message("REGISTRATION", log_buffer);
}

// Function to log cache operations
// void log_cache_operation(const char* operation, const char* path, const char* result) {
//     char log_buffer[MAX_LOG_LENGTH];
//     snprintf(log_buffer, sizeof(log_buffer), 
//              "Cache %s - Path: %s, Result: %s",
//              operation, path, result);
//     log_message("CACHE", log_buffer);
// }

// // Function to log path operations
// void log_path_operation(const char* operation, const char* path, const char* result) {
//     char log_buffer[MAX_LOG_LENGTH];
//     snprintf(log_buffer, sizeof(log_buffer), 
//              "Path %s - Path: %s, Result: %s",
//              operation, path, result);
//     log_message("PATH", log_buffer);
// }


void clear_log_file() {
    FILE* log_file = fopen("naming_server_log.txt", "w");  // Open in write mode to clear contents
    if (log_file == NULL) {
        printf("Error opening log file (ERROR CODE %d)\n",ERR_OPENING);
        perror("Error clearing log file");
        return;
    }
    fclose(log_file);
    printf("Log file cleared successfully.\n");
}

void handle_shutdown(int sig) {
    printf("\nReceived signal %d. Shutting down Naming Server...\n", sig);
    clear_log_file();  // Clear log file contents
    exit(0);  // Exit the program
}

// int find_client_info_from_log(const char *filename, char *client_ip, int *client_port, int* client_sock_fd) {
//     FILE *log_file = fopen(LOG_FILE, "r");
//     if (!log_file) {
//         printf("Error opening log file (ERROR CODE %d)\n",ERR_OPENINING);
//         perror("Error opening log file");
//         return -1;
//     }

//     char line[512];
//     while (fgets(line, sizeof(line), log_file)) {
//         // Look for a WRITE request in the log
//         if (strstr(line, "WRITE") && strstr(line, filename)) {
//             // sscanf(line, "%*[^I]IP: %[^,], Port: %d, Socket FD: %d", client_ip, &client_port, &client_sock_fd);
//             if (sscanf(line, "%*[^I]IP: %[^,], Port: %d, Socket FD: %d", client_ip, client_port, client_sock_fd) == 3) {
//                 fclose(log_file);
//                 return 0; // Success
//             }
//             // fclose(log_file);
//             // return 0;  // Found the client info
//         }
//     }

//     fclose(log_file);
//     return -1;  // Client info not found
// }


int find_client_info_from_log(const char *filename, char *client_ip, int *client_port, int *client_sock_fd) {
    FILE *log_file = fopen(LOG_FILE, "r");
    if (!log_file) {
        printf("Error opening log file (ERROR CODE %d)\n", ERR_OPENING);
        perror("Error opening log file");
        return -1;
    }

    // Seek to the end of the file
    fseek(log_file, 0, SEEK_END);

    // Find the file size
    long file_size = ftell(log_file);
    if (file_size == 0) {
        fclose(log_file);
        return -1;  // Empty file
    }

    // Buffer for reading lines
    char line[BUFFER_SIZE];
    long offset = file_size;

    while (offset > 0) {
        // Move backwards, one byte at a time
        fseek(log_file, --offset, SEEK_SET);

        // Check for newline to identify the start of a line
        if (fgetc(log_file) == '\n' || offset == 0) {
            // Read the current line
            if (fgets(line, sizeof(line), log_file)) {
                // Check for WRITE request and the specific filename
                if (strstr(line, "WRITE") && strstr(line, filename)) {
                    // Example log entry:
                    // [2024-11-17 15:41:27] [REQUEST] Client Request - IP: 192.168.12.48, Port: 48964, Socket FD: 15, ACK: 1001, Request: WRITE /home/test1.txt

                    // Extract IP, Port, and Socket FD
                    if (sscanf(line, "%*[^I]IP: %[^,], Port: %d, Socket FD: %d", client_ip, client_port, client_sock_fd) == 3) {
                        fclose(log_file);
                        return 0;  // Success
                    }
                }
            }
        }
    }

    fclose(log_file);
    return -1;  // No matching entry found
}

// Main function
int main()
{
    init_ss_connection_manager(); // Initialize the SS connection manager
    // Initialize request queue
    init_request_queue();
    signal(SIGINT, handle_shutdown);
    printf("Naming Server started. Press CTRL+C to stop and clear log file.\n");
    init_logging();

    // Create worker threads
    pthread_t worker_threads[4]; // Create 4 worker threads
    for (int i = 0; i < 4; i++)
    {
        pthread_create(&worker_threads[i], NULL, process_requests, NULL);
    }
    char ip[INET_ADDRSTRLEN];
    find_ip(ip);
    printf("Naming Server IP: %s\n", ip);
    start_naming_server(NS_PORT); // Start the naming server on the defined port

    free_lru_cache(cache); // Free the cache memory
    close_logging();
    return 0;
}

int find_ss_connection(const char *ip, int port)
{
    pthread_mutex_lock(&ss_manager.lock);

    for (int i = 0; i < MAX_SS_CONNECTIONS; i++)
    {
        if (ss_manager.connections[i].is_active && strcmp(ss_manager.connections[i].ip_address, ip) == 0 && ss_manager.connections[i].port == port)
        {
            pthread_mutex_unlock(&ss_manager.lock);
            return i;
        }
    }

    pthread_mutex_unlock(&ss_manager.lock);
    return -1;
}