#ifndef CLIENT_MAP_H
#define CLIENT_MAP_H

#include <netinet/in.h>
#include <stdbool.h>

#define FREE_INIT_LEN 64

typedef struct client_info {
	/**
	 * client_id is part of this struct because multiple keys could be mapped 
	 * to the same bucket.
	 */
	uint64_t client_id;
	struct sockaddr_in client_addr;
	socklen_t client_addr_len;
	char username[16];
	struct client_info *next;
} ClientInfo;

/* TODO: keep track of total number of clients to limit 
 * the number of clients served per thread. */
typedef struct {
	size_t buckets_cap;
	ClientInfo **buckets;

	size_t free_cap;
	size_t free_len;
	ClientInfo **free;
} ClientMap;

/* `cap` must be a power of two. */
void client_map_init(ClientMap *cm, size_t cap);

/**
 * Searches the map for a ClientInfo matching the given client_id, sets the info to
 * the matching ClientInfo, and returns true if the search was successful. Returns
 * false otherwise.
 */
bool client_map_new_entry(ClientMap *cm, uint64_t client_id, ClientInfo **info);

/**
 * Returns a ClientInfo corresponding to the given client_id.
 * Returns NULL if search was unsuccessful.
 */
ClientInfo *client_map_get(ClientMap *cm, uint64_t client_id);

/* Returns true if deletion was successful, and false otherwise. */
bool client_map_delete(ClientMap *cm, uint64_t client_id);

void client_map_deinit(ClientMap *cm);

#endif
