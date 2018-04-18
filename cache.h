#ifndef MARJ_CACHE_H
#define MARJ_CACHE_H

#include <stdlib.h>
#include "csapp.h"

#define MAX_CACHE_SIZE 1049000

typedef struct CachedItem CachedItem;

struct CachedItem {
  char *url;
  char *item_p;
  size_t size;
  CachedItem *prev;
  CachedItem *next;
};

typedef struct {
  size_t size;
  CachedItem* first;
  CachedItem* last;
} CacheList;

extern void cache_init(CacheList *list);
extern void cache_URL(char *URL, void *item, size_t size, CacheList *list);
extern void evict(CacheList *list);
extern CachedItem *find(char *URL, CacheList *list);
//extern CachedItem get_cache(char *URL, CacheList *list);
extern void move_to_front(CachedItem* item, CacheList *list);
extern void print_URLs(CacheList *list);
extern void cache_destruct(CacheList *list);

#endif