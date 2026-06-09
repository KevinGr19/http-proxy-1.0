/**
 * Cache:
 * - Simple resizable hashmap with linear probing
 * - Entries are doubly-linked together to implement LRU eviction policy
 * 
 * Readers-Writers:
 * - Multiple readers or one writer at a time
 * - Priority to writers (data already allocated: writing is constant apart from hashmap placing)
 */

#include "logging.h"
#include "cache.h"

#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <strings.h>
#define __USE_XOPEN2K
#include <pthread.h>

#define MAX_CACHE_SIZE 1049000
#define MAX_OBJECT_SIZE 102400
#define DEFAULT_EXPIRES_OFFSET 60*5

#define HASHMAP_MIN_CAPACITY 17
#define HASHMAP_LOAD_SHRINK 0.3
#define HASHMAP_LOAD_EXPAND 0.7

typedef unsigned long long u64;
typedef u64 hash_t;

typedef struct cache_entry cache_entry;
struct cache_entry{
    cache_response response;
    cache_entry* prev;
    cache_entry* next;
    cache_key key;
    void** hashmap_slot;
    hash_t hash;
};

typedef struct{
    void** array;
    size_t size;
    size_t capacity;
} hashmap;

hashmap map;
cache_entry HASHMAP_TOMBSTONE = {0};

size_t cache_size;
cache_entry *lru_entry, *mru_entry;
pthread_rwlock_t rwlock;
pthread_mutex_t rmutex;

static int try_send_response(int fd, cache_key* key);
static int store_response(cache_key* key, cache_response* response);
static int update_response_headers(cache_key* key, cache_response* response);

static cache_entry* list_pop_lru(void);
static void list_set_mru(cache_entry* entry);
static void list_remove(cache_entry* entry);
static void free_entry(cache_entry* entry);

static void hashmap_create(size_t init_capacity);
static void hashmap_resize(size_t capacity);
static cache_entry* hashmap_get_entry(cache_key* key);
static int hashmap_store_entry(cache_entry* entry);
static int hashmap_delete_entry(cache_entry* entry);

static u64 next_prime(u64 number);
static hash_t hash_key(cache_key* key);
static bool are_keys_equal(cache_key* k1, cache_key* k2);

void cache_init(void){
    cache_size = 0;
    lru_entry = mru_entry = NULL;
    hashmap_create(next_prime(64));

    pthread_rwlockattr_t attr;
    pthread_rwlockattr_init(&attr);
    pthread_rwlockattr_setkind_np(&attr, PTHREAD_RWLOCK_PREFER_WRITER_NP);
    if(pthread_rwlock_init(&rwlock, &attr) == -1){
        log_fail_errno(fatal, pthread_rwlock_init);
        exit(1);
    }

    if(pthread_mutex_init(&rmutex, NULL) == -1){
        log_fail_errno(fatal, pthread_mutex_init);
        exit(1);
    }
}

int cache_try_send_response(int fd, cache_key* key){
    pthread_rwlock_rdlock(&rwlock);
    int rc = try_send_response(fd, key);
    pthread_rwlock_unlock(&rwlock);
    return rc;
}

int cache_store_response(cache_key* key, cache_response* response){
    pthread_rwlock_wrlock(&rwlock);
    int rc = store_response(key, response);
    pthread_rwlock_unlock(&rwlock);
    return rc;
}

int cache_update_response_headers(cache_key* key, cache_response* response){
    pthread_rwlock_wrlock(&rwlock);
    int rc = update_response_headers(key, response);
    pthread_rwlock_unlock(&rwlock);
    return rc;
}

static int try_send_response(int fd, cache_key* key){
    cache_entry* entry = hashmap_get_entry(key);
    if(!entry) return ECACHE_NOENTRY;

    time_t curr_time = time(NULL);
    if(curr_time > entry->response.expires) return ECACHE_NOENTRY;

    pthread_mutex_lock(&rmutex);
    list_set_mru(entry);
    pthread_mutex_unlock(&rmutex);

    int rc;
    heap_buf send_buf;
    heap_buf_init(&send_buf);

    http_status_line* line = entry->response.status_line;
    rc = write_buf_httpline_fmt(
        &send_buf, "HTTP/%d.%d %d %s",
        line->version.major, line->version.minor, line->code, line->reason_phrase
    );
    if(rc == -1) goto ret;

    rc = header_list_write_buf(&send_buf, entry->response.headers);
    if(rc == -1) goto ret;

    rc = write_message(fd, send_buf.buf, send_buf.size, entry->response.body, entry->response.content_length);

    ret:
    heap_buf_free(&send_buf);
    return rc == -1 ? ECACHE_ERRNO : 0;
}

static int store_response(cache_key* key, cache_response* response){
    if(response->content_length > MAX_OBJECT_SIZE)
        return ECACHE_REFUSED;
        
    while(cache_size + response->content_length > MAX_CACHE_SIZE){
        cache_entry* lru = list_pop_lru();
        if(!lru){
            fatal_tr("Need more cache space, but no LRU entry to delete");
            exit(1);
        }
        free_entry(lru);
    }

    cache_entry* prev_entry = hashmap_get_entry(key);
    if(prev_entry){
        cache_size += response->content_length - prev_entry->response.content_length;
        prev_entry->response = *response;
        list_set_mru(prev_entry);
        debug_tr("Updated cache entry (%s:%s %s)", key->hostname, key->port, key->uri);
        return 0;
    }

    size_t hostname_len = strlen(key->hostname)+1;
    size_t port_len = strlen(key->port)+1;
    size_t uri_len = strlen(key->uri)+1;

    cache_entry* entry = malloc(sizeof(cache_entry) + hostname_len + port_len + uri_len);
    if(!entry) return ECACHE_ERRNO;

    char* key_buf = (char*)entry + sizeof(cache_entry);
    *entry = (cache_entry){
        .response = *response,
        .key = (cache_key){
            .hostname = &key_buf[0],
            .port = &key_buf[hostname_len],
            .uri = &key_buf[hostname_len+port_len],
        },
    };

    memcpy(&key_buf[0], key->hostname, hostname_len);
    memcpy(&key_buf[hostname_len], key->port, port_len);
    memcpy(&key_buf[hostname_len+port_len], key->uri, uri_len);

    if(hashmap_store_entry(entry) == -1) return ECACHE_ERRNO;
    list_set_mru(entry);
    cache_size += response->content_length;

    debug_tr("Added cache entry (%s:%s %s)", key->hostname, key->port, key->uri);

    if(!(entry->response.received_headers & HDR_EXPIRES)){
        entry->response.expires = time(NULL) + DEFAULT_EXPIRES_OFFSET;
        debug_tr("Setting default expires date to cache entry: expire in %zu seconds", DEFAULT_EXPIRES_OFFSET);
    }

    debug_tr("Cache state: entries=%zu capacity=%zu cache_size=%zuB", map.size, map.capacity, cache_size);
    return 0;
}

static int update_response_headers(cache_key* key, cache_response* response){
    cache_entry* entry = hashmap_get_entry(key);
    if(!entry) return ECACHE_NOENTRY;

    int rc = header_list_update(entry->response.headers, response->headers);
    if(rc == -1) return ECACHE_ERRNO;

    list_set_mru(entry);
    if((response->received_headers & HDR_EXPIRES))
        entry->response.expires = response->expires;

    return 0;
}

static cache_entry* list_pop_lru(void){
    if(!lru_entry) return NULL;
    cache_entry* entry = lru_entry;
    list_remove(lru_entry);
    return entry;
}

static void list_set_mru(cache_entry* entry){
    if(entry == mru_entry) return;
    list_remove(entry);

    if(!mru_entry){
        lru_entry = entry;
        mru_entry = entry;
    }
    else{
        entry->prev = mru_entry;
        mru_entry->next = entry;
        mru_entry = entry;
    }
}

static void list_remove(cache_entry* entry){
    if(entry->prev) entry->prev->next = entry->next;
    if(entry->next) entry->next->prev = entry->prev;
    if(entry == lru_entry) lru_entry = entry->next;
    if(entry == mru_entry) mru_entry = entry->prev;
    entry->prev = entry->next = NULL;
}

static void free_entry(cache_entry* entry){
    list_remove(entry);
    
    if(entry->hashmap_slot) hashmap_delete_entry(entry);
    cache_size -= entry->response.content_length;
    
    if(entry->response.body) free(entry->response.body);
    header_list_free(entry->response.headers);
    free(entry);
}

static void hashmap_create(size_t init_capacity){
    map = (hashmap){
        .array = NULL,
        .size = 0,
        .capacity = 0,
    };
    hashmap_resize(init_capacity);
}

static void hashmap_resize(size_t capacity){
    if(capacity < HASHMAP_MIN_CAPACITY || map.capacity == capacity) return;
    debug_tr("Resizing cache hashmap (size=%zu capacity=%zu) to %zu", map.size, map.capacity, capacity);

    void** new_array = calloc(capacity, sizeof(void*));
    if(!new_array){
        log_fail_errno(fatal, calloc);
        exit(1);
    }

    hashmap old_map = map;
    map = (hashmap){
        .array = new_array,
        .size = 0,
        .capacity = capacity,
    };
    if(!old_map.array) return;

    size_t relocated = 0;
    for(size_t i = 0; i < old_map.capacity && relocated < old_map.size; i++){
        if(old_map.array[i] == NULL || old_map.array[i] == &HASHMAP_TOMBSTONE) continue;
        if(hashmap_store_entry((cache_entry*)old_map.array[i]) == -1){
            log_fail_errno(fatal, hashmap_store_entry);
            exit(1);
        }
        relocated++;
    }

    free(old_map.array);
    return;
}

static cache_entry* hashmap_get_entry(cache_key* key){
    hash_t hash = hash_key(key);
    size_t init_slot = hash % map.capacity;

    size_t visited = 0;
    for(size_t i = 0; i < map.capacity && visited < map.size; i++){
        cache_entry* entry = map.array[(init_slot+i)%map.capacity];
        if(entry == NULL || entry == &HASHMAP_TOMBSTONE) continue;

        visited++;
        if(entry->hash != hash) continue;
        if(are_keys_equal(key, &entry->key)) return entry;
    }
    return NULL;
}

static int hashmap_store_entry(cache_entry* entry){
    if((double)map.size >= map.capacity * HASHMAP_LOAD_EXPAND)
        hashmap_resize(next_prime(map.capacity * 2));
    
    entry->hash = hash_key(&entry->key);
    entry->hashmap_slot = NULL;
    size_t init_slot = entry->hash % map.capacity;

    size_t i;
    for(i = init_slot; i < map.capacity; i++){
        if(map.array[i] == NULL || map.array[i] == &HASHMAP_TOMBSTONE) goto store;
    }
    for(i = 0; i < init_slot; i++){
        if(map.array[i] == NULL || map.array[i] == &HASHMAP_TOMBSTONE) goto store;
    }
    return -1;

    store:
    map.array[i] = entry;
    map.size++;
    entry->hashmap_slot = &map.array[i];
    return 0;
}

static int hashmap_delete_entry(cache_entry* entry){
    if(!entry->hashmap_slot) return -1;

    *entry->hashmap_slot = &HASHMAP_TOMBSTONE;
    entry->hashmap_slot = NULL;
    map.size--;

    if((double)map.size <= map.capacity * HASHMAP_LOAD_SHRINK)
        hashmap_resize(next_prime(map.capacity / 2));
    
    return 0;
}

static u64 next_prime(u64 number){
    if(number <= 2) return 2;

    while(1){
        bool is_prime = true;
        for(u64 i = 2; i*i <= number; i++){
            if(number % i == 0){
                is_prime = false;
                break;
            }
        }
        if(is_prime) return number;
        number++;
    }
}

// djb2 hash
static hash_t hash_key(cache_key* key){
    hash_t hash = 5381;
    
    const char* strs[] = {key->hostname, key->port, key->uri};
    int n = sizeof(strs)/sizeof(strs[0]);
    
    for(int i = 0; i < n; i++){
        const char* str = strs[i];
        hash_t c;

        while((c = *str++))
            hash = ((hash << 5) + hash) + c; // hash * 33 + c
    }

    return hash;
}

static bool are_keys_equal(cache_key* k1, cache_key* k2){
    return
        strcmp(k1->hostname, k2->hostname) == 0 &&
        strcmp(k1->port, k2->port) == 0 &&
        strcmp(k1->uri, k2->uri) == 0;
}