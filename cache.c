#include "cache.h"

// Initialize the LRU cache
LRUCache* init_lru_cache(int capacity) {
    LRUCache* cache = (LRUCache*)malloc(sizeof(LRUCache));
    cache->capacity = capacity;
    cache->size = 0;
    cache->head = NULL;
    cache->tail = NULL;
    
    // Initialize hash table
    memset(cache->hash, 0, sizeof(CacheNode*) * CACHE_SIZE);
    
    // Initialize mutex
    pthread_mutex_init(&cache->lock, NULL);
    
    return cache;
}

// Hash function for paths
unsigned int cache_hash(const char* path) {
    unsigned int hash = 0;
    while (*path) {
        hash = (hash * 31) + *path++;
    }
    return hash % CACHE_SIZE;
}

// Move node to front (most recently used)
void move_to_front(LRUCache* cache, CacheNode* node) {
    if (node == cache->head) {
        return; // Already at front
    }
    
    // Remove from current position
    if (node->prev) {
        node->prev->next = node->next;
    }
    if (node->next) {
        node->next->prev = node->prev;
    }
    if (node == cache->tail) {
        cache->tail = node->prev;
    }
    
    // Move to front
    node->next = cache->head;
    node->prev = NULL;
    if (cache->head) {
        cache->head->prev = node;
    }
    cache->head = node;
    if (!cache->tail) {
        cache->tail = node;
    }
}

// Create new cache node
CacheNode* create_node(const char* path, const char* ss_ip, int ss_port) {
    CacheNode* node = (CacheNode*)malloc(sizeof(CacheNode));
    strncpy(node->path, path, sizeof(node->path) - 1);
    strncpy(node->ss_ip, ss_ip, sizeof(node->ss_ip) - 1);
    node->ss_port = ss_port;
    node->prev = NULL;
    node->next = NULL;
    return node;
}

// Add or update entry in cache
void cache_put(LRUCache* cache, const char* path, const char* ss_ip, int ss_port) {
    pthread_mutex_lock(&cache->lock);
    
    unsigned int hash_key = cache_hash(path);
    CacheNode* existing = cache->hash[hash_key];
    
    if (existing) {
        // Update existing entry
        strncpy(existing->ss_ip, ss_ip, sizeof(existing->ss_ip) - 1);
        existing->ss_port = ss_port;
        move_to_front(cache, existing);
    } else {
        // Create new entry
        CacheNode* new_node = create_node(path, ss_ip, ss_port);
        
        // If cache is full, remove least recently used
        if (cache->size >= cache->capacity) {
            CacheNode* lru = cache->tail;
            unsigned int lru_hash = cache_hash(lru->path);
            cache->hash[lru_hash] = NULL;
            
            cache->tail = lru->prev;
            if (cache->tail) {
                cache->tail->next = NULL;
            }
            free(lru);
            cache->size--;
        }
        
        // Add new node to front
        new_node->next = cache->head;
        if (cache->head) {
            cache->head->prev = new_node;
        }
        cache->head = new_node;
        if (!cache->tail) {
            cache->tail = new_node;
        }
        
        cache->hash[hash_key] = new_node;
        cache->size++;
    }
    
    pthread_mutex_unlock(&cache->lock);
}

// Get entry from cache
bool cache_get(LRUCache* cache, const char* path, char* ss_ip, int* ss_port) {
    pthread_mutex_lock(&cache->lock);
    
    unsigned int hash_key = cache_hash(path);
    CacheNode* node = cache->hash[hash_key];
    
    if (node && strcmp(node->path, path) == 0) {
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
void free_lru_cache(LRUCache* cache) {
    pthread_mutex_lock(&cache->lock);
    
    CacheNode* current = cache->head;
    while (current) {
        CacheNode* next = current->next;
        free(current);
        current = next;
    }
    
    pthread_mutex_unlock(&cache->lock);
    pthread_mutex_destroy(&cache->lock);
    free(cache);
}

// Print cache contents (for debugging)
void print_cache_contents(LRUCache* cache) {
    pthread_mutex_lock(&cache->lock);
    
    printf("\nCache Contents (Most Recent First):\n");
    CacheNode* current = cache->head;
    while (current) {
        printf("Path: %s -> SS: %s:%d\n", 
               current->path, current->ss_ip, current->ss_port);
        current = current->next;
    }
    printf("Cache size: %d/%d\n", cache->size, cache->capacity);
    
    pthread_mutex_unlock(&cache->lock);
}