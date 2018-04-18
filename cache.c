#include "cache.h"

extern void cache_init(CacheList *list){
    list->first = NULL;
    list->last = NULL;
    list->size = 0;
}

extern void cache_URL(char *URL, void *item, size_t size, CacheList *list){
    //check if the max cache size has been reached. If it has, evict the last entry.
    //make sure that reading and writing moves things to the beginning
    printf("URL: %s\n", URL);
    if(size > MAX_CACHE_SIZE){
        return;
    }

    while(list->size + size > MAX_CACHE_SIZE){
        evict(list);
    }    
    
    //create the cacheditem struct
    CachedItem* cacheEntry = (CachedItem*)Malloc(strlen(URL) + strlen(item) +
        sizeof(size_t) + 2* sizeof(CachedItem*) + 2); 

    char* host = (char*)Malloc(strlen(URL)+1);
    char* data = (char*)Malloc(strlen(item)+1);

    strcpy(host, URL);
    strcpy(data, item);

    cacheEntry->url = host;
    cacheEntry->item_p = data;
    cacheEntry->size = size;
    
    //if this is the first item in the list
    if(list->size == 0){
        cacheEntry->prev = NULL;
        cacheEntry->next = NULL;

        list->first = cacheEntry;
        list->last = cacheEntry;        
    }
    else{
        CachedItem* firstEntry = list->first;
        firstEntry->prev = cacheEntry;
        cacheEntry->next = firstEntry;
        list->first = cacheEntry;
    }
    list->size+= size;
}

extern void evict(CacheList *list){
    CachedItem* lastEntry = list->last;
    lastEntry->prev->next = NULL;
    list->last = lastEntry->prev;

    list->size-= lastEntry->size;

    Free(lastEntry);
}

extern CachedItem *find(char *URL, CacheList *list){
    CachedItem* item = list->first;
    while(item != NULL){
        if(!strcmp(item->url, URL)){
            move_to_front(item, list);
            return item;
        }
        item = item->next;
    }    
    //cache miss
    return NULL;
}

//extern CachedItem get_cache(char *URL, CacheList *list);
extern void move_to_front(CachedItem* item, CacheList *list){
    //if the item is at the front, do nothing
    if(item != list->first){
        //if the item is at the end.
        if(item == list->last){
            item->prev->next = NULL;
            list->last = item->prev;
        }
        else{
            item->prev->next = item->next;
            item->next->prev = item->prev;
        }

        list->first->prev = item;
        item->next = list->first;
        item->prev = NULL;
        list->first = item;
    }
}

//for debugging
extern void print_URLs(CacheList *list){
    CachedItem* item = list->first;
    while(item != NULL){
        printf("%s\n", item->url);
        item = item->next;
    }
    printf("\n");
}

extern void cache_destruct(CacheList *list){
    CachedItem* item = list->first;
    while(item != NULL){
        CachedItem* next = item->next;
        Free(item->url);
        Free(item->item_p);
        Free(item);
        item = next;
    }
}