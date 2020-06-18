#include "stdlib.h"
#include "csrv.h"

// Adapted version of the djb2 hash described here:
// http://www.cse.yorku.ca/~oz/hash.html
size_t csrv_djb2_hash(char *str) {
  size_t hash = 5381;
  
  for(size_t idx = 0; str[idx] != '\0'; idx++) {
    hash = ((hash << 5) + hash) + ((uint8_t *) str)[idx];
  }

  return hash;
}

int csrv_str_map_init(struct CsrvStrMap *map) {
  if(map->size == 0) {
    map->size = CSRV_DEFAULT_MAP_SIZE;
  }

  map->n_items = 0;
  map->n_collisions = 0;
  map->hashmap = (char **) calloc(map->size, sizeof(char *));
  if(map->hashmap == NULL) {
    return -1;
  }
  
  map->used_idx_sz = 0;
  map->used_idx = (size_t *) malloc(sizeof(size_t) * map->size);
  if(map->used_idx == NULL) {
    return -1;
  }

  return 0;
}

void csrv_str_map_add(struct CsrvStrMap *map, char *key, char *value) {
  size_t idx = csrv_djb2_hash(key) % map->size;
  if(map->hashmap[idx] != NULL) {
    map->n_collisions++;
    // TODO add error handler
    return;
  }
  map->hashmap[idx] = value;
  map->n_items++;
  map->used_idx[map->used_idx_sz++] = idx;
}

char *csrv_str_map_get(struct CsrvStrMap *map, char *key) {
  size_t idx = csrv_djb2_hash(key) % map->size;
  return map->hashmap[idx];
}

void csrv_str_map_cleanup(struct CsrvStrMap *map) {
  for(size_t i = 0; i < map->used_idx_sz; i++) {
    size_t idx = map->used_idx[i];
    free(map->hashmap[idx]);
  }
  
  free(map->used_idx);
  free(map->hashmap);
}

