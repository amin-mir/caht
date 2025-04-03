#include "cid_set.h"
#include <stdio.h>
#include <stdlib.h>

#define INIT_CAP 8
#define LOAD_FACTOR 0.75
#define HASH_MULT 11400714819323198485llu
#define EMPTY_VAL UINT64_MAX

static inline size_t hash(uint64_t cid, size_t cap) {
  return (cid * HASH_MULT) & (cap - 1);
}

void cid_set_init(struct cid_set *set) {
  set->ids = malloc(INIT_CAP * sizeof(uint64_t));
  if (set->ids == NULL) {
    perror("cid_set_init malloc");
    exit(EXIT_FAILURE);
  }

  set->len = 0;
  set->cap = INIT_CAP;

  for (size_t i = 0; i <= set->cap; i++) {
    set->ids[i] = EMPTY_VAL;
  }
}

bool cid_set_exists(struct cid_set *set, uint64_t id) {
  size_t cap = set->cap;
  size_t i = hash(id, cap);
  while (set->ids[i] != EMPTY_VAL) {
    if (set->ids[i] == id) {
      return true;
    }
    i = (i + 1) & (cap - 1);
  }

  return false;
}

static void insert(struct cid_set *set, uint64_t id) {
  size_t i = hash(id, set->cap);
  while (set->ids[i] != EMPTY_VAL) {
    /* Already exists. */
    if (set->ids[i] == id) {
      return;
    }
    i = (i + 1) & (set->cap - 1);
  }
  set->ids[i] = id;
  set->len++;
}

void cid_set_grow(struct cid_set *set) {
  uint64_t *old_ids = set->ids;
  size_t old_cap = set->cap;

  set->cap = set->cap * 2;
  set->ids = malloc(set->cap * sizeof(uint64_t));
  if (set->ids == NULL) {
    perror("cid_set_grow malloc");
    exit(EXIT_FAILURE);
  }

  for (size_t i = 0; i < set->cap; i++) {
    set->ids[i] = EMPTY_VAL;
  }

  /* insert will adjust the len. */
  set->len = 0;
  for (size_t i = 0; i < old_cap; i++) {
    if (old_ids[i] != EMPTY_VAL) {
      insert(set, old_ids[i]);
    }
  }

  free(old_ids);
}

void cid_set_insert(struct cid_set *set, uint64_t id) {
  if (id == EMPTY_VAL) {
    fprintf(stderr, "illegal value for id (UINT64_MAX): %lu\n", EMPTY_VAL);
    exit(EXIT_FAILURE);
  }

  if ((double)set->len >= set->cap * LOAD_FACTOR) {
    cid_set_grow(set);
  }

  insert(set, id);
}
