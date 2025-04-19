#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "client_map.h"
#include "utils.h"

void client_info_log(ClientInfo *info, char *msg) {
	char client_ip[INET_ADDRSTRLEN];
	const char *dst = inet_ntop(
		AF_INET, 
		&info->client_addr.sin_addr, 
		client_ip, 
		INET_ADDRSTRLEN
	);
	if (dst == NULL) fatal_error("inet_ntop");
	uint16_t port = ntohs(info->client_addr.sin_port);
	printf("[%s:%d] client_id=%lu => %s\n", client_ip, port, info->client_id, msg);
}

static inline size_t hash(size_t cap, uint64_t client_id) {
	return client_id & (cap - 1);
}

void client_map_init(ClientMap *cm, size_t cap) {
	if (cap == 0 || (cap & (cap-1)) != 0) {
		fprintf(stderr, "cap must be a power of 2: %zu\n", cap);
		exit(EXIT_FAILURE);
	}

	cm->buckets_cap = cap;
	cm->buckets = calloc(cap, sizeof(ClientInfo *));

	cm->free_cap = FREE_INIT_LEN;
	cm->free_len = 0;
	cm->free = calloc(FREE_INIT_LEN, sizeof(ClientInfo *));

	if (cm->buckets == NULL || cm->free == NULL) {
		perror("clinet_map_init: calloc");
		exit(EXIT_FAILURE);
	}
}

bool client_map_get_new(ClientMap *cm, uint64_t client_id, ClientInfo **info) {
	size_t i = hash(cm->buckets_cap, client_id);
	ClientInfo *node = cm->buckets[i];

	/* Make sure client_id doesn't already exist. */
	while (node != NULL) {
		if (node->client_id == client_id) return false;
		node = node->next;
	}

	/* Attempt to get a ClientInfo from free list. */
	if (cm->free_len != 0) {
		node = cm->free[0];
		cm->free[0] = cm->free[cm->free_len-1];
		cm->free_len -= 1;
	}
	/* Malloc a new one if free is empty. */
	else { 
		node = malloc(sizeof(ClientInfo));
		if (node == NULL) {
			perror("client_map_get_new: malloc");
			exit(EXIT_FAILURE);
		}
	}

	// TODO: init client_addr
	// memset(&op->client_addr, 0, sizeof(op->client_addr));
	// op->client_addr_len = sizeof(op->client_addr);
	ClientInfo *old_head = cm->buckets[i];
	cm->buckets[i] = node;
	node->next = old_head;
	node->client_id = client_id;
	*info = node;

	return true;
}

ClientInfo *client_map_get(ClientMap *cm, uint64_t client_id) {
	size_t i = hash(cm->buckets_cap, client_id);
	ClientInfo *node = cm->buckets[i];
	while (node != NULL) {
		if (node->client_id == client_id) return node;
		node = node->next;
	}
	return NULL;
}

/**
 * This function zeores out the ClientInfo, so make sure to copy out 
 * the values you need such as next before calling this function.
 */
void client_map_add_free(ClientMap *cm, ClientInfo *info) {
	if (cm->free_len == cm->free_cap) {
		cm->free_cap *= 2;
		cm->free = realloc(cm->free, cm->free_cap * sizeof(ClientInfo *));
		if (cm->free == NULL) {
			perror("client_map_add_free: realloc");
			exit(EXIT_FAILURE);
		}
	}

	memset(info, 0, sizeof(ClientInfo));
	cm->free[cm->free_len] = info;
	cm->free_len++;
}

bool client_map_delete(ClientMap *cm, uint64_t client_id) {
	size_t i = hash(cm->buckets_cap, client_id);
	ClientInfo *head = cm->buckets[i];

	/* Handle deletion of the head of the linked list. */
	if (head->client_id == client_id) {
		cm->buckets[i] = head->next;
		client_map_add_free(cm, head);
		return true;
	}

	ClientInfo *prev = head;
	ClientInfo *cur = head->next;
	while (cur != NULL) {
		if (cur->client_id == client_id) {
			prev->next = cur->next;
			client_map_add_free(cm, cur);
			return true;
		}
		prev = cur;
		cur = cur->next;
	}

	return false;
}

void client_map_deinit(ClientMap *cm) {
	ClientInfo *node, *next;

	/* Go inside each bucket and free the nodes individually. */
	for (size_t i = 0; i < cm->buckets_cap; i++) {
		node = cm->buckets[i];
		while (node != NULL) {
			next = node->next;
			free(node);
			node = next;
		}
	}
	free(cm->buckets);

	/** 
	 * We need to follow the links in the free list as free only
	 * stores individual nodes.
	 */
	for (size_t i = 0; i < cm->free_len; i++) {
		free(cm->free[i]);
	}
	free(cm->free);
}
