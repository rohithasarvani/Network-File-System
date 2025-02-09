// Add to naming_server.h
#include <stdbool.h>
#include <stdio.h>
#include <pthread.h>
#include <stdlib.h>

#define CACHE_SIZE 1000  // Maximum number of entries in the cache
#define INET_ADDRSTRLEN 16        // Length of the string for IP address- IPv4

typedef struct CacheNode {
    char path[256];              // File/folder path
    char ss_ip[INET_ADDRSTRLEN]; // Storage server IP
    int ss_port;                 // Storage server port
    struct CacheNode* prev;      // Previous node in DLL
    struct CacheNode* next;      // Next node in DLL
} CacheNode;

typedef struct {
    CacheNode* head;            // Most recently used
    CacheNode* tail;            // Least recently used
    int size;                   // Current number of entries
    int capacity;               // Maximum capacity
    pthread_mutex_t lock;       // Mutex for thread safety
    CacheNode* hash[CACHE_SIZE]; // Hash table for O(1) lookup
} LRUCache;

LRUCache* init_lru_cache(int capacity);
unsigned int cache_hash(const char* path);
void move_to_front(LRUCache* cache, CacheNode* node) ;
CacheNode* create_node(const char* path, const char* ss_ip, int ss_port);
void cache_put(LRUCache* cache, const char* path, const char* ss_ip, int ss_port);
bool cache_get(LRUCache* cache, const char* path, char* ss_ip, int* ss_port);
void free_lru_cache(LRUCache* cache) ;
void print_cache_contents(LRUCache* cache);