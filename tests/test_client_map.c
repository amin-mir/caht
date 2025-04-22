#include <criterion/criterion.h>
#include <criterion/new/assert.h>

#include "../client_map.h"

Test(client_map, operations) {
	ClientMap cm;
	client_map_init(&cm, 16);

	/* These client_ids map to the same bucket. */
	ClientInfo *info;

	cr_assert(client_map_new_entry(&cm, 1, &info));
	cr_assert(info->client_id == 1);
	info->client_addr_len = 1;

	cr_assert(client_map_new_entry(&cm, 17, &info));
	cr_assert(info->client_id == 17);
	info->client_addr_len = 17;

	cr_assert(client_map_new_entry(&cm, 31, &info));
	cr_assert(info->client_id == 31);
	info->client_addr_len = 31;

	cr_assert(not(client_map_new_entry(&cm, 1, &info)));

	cr_assert(client_map_delete(&cm, 31));
	cr_assert(client_map_get(&cm, 1) != NULL);
	cr_assert(client_map_get(&cm, 17) != NULL);
	cr_assert(client_map_get(&cm, 31) == NULL);

	cr_assert(client_map_delete(&cm, 17));
	cr_assert(client_map_get(&cm, 1) != NULL);
	cr_assert(client_map_get(&cm, 17) == NULL);
	cr_assert(client_map_get(&cm, 31) == NULL);

	cr_assert(client_map_delete(&cm, 1));
	cr_assert(client_map_get(&cm, 1) == NULL);
	cr_assert(client_map_get(&cm, 17) == NULL);
	cr_assert(client_map_get(&cm, 31) == NULL);

	cr_assert(eq(sz, cm.free_cap, FREE_INIT_LEN));
	cr_assert(eq(sz, cm.free_len, 3));

	client_map_new_entry(&cm, 1, &info);
	cr_assert(eq(u64, info->client_id, 1));
	cr_assert(eq(u64, info->client_addr_len, 0));

	client_map_new_entry(&cm, 2, &info);
	cr_assert(eq(u64, info->client_id, 2));
	cr_assert(eq(u64, info->client_addr_len, 0));

	client_map_new_entry(&cm, 3, &info);
	cr_assert(eq(u64, info->client_id, 3));
	cr_assert(eq(u64, info->client_addr_len, 0));

	cr_assert(eq(sz, cm.free_cap, FREE_INIT_LEN));
	cr_assert(eq(sz, cm.free_len, 0));
}

Test(client_map, cap_adjust) {
	ClientMap cm;
	client_map_init(&cm, 16);

	size_t insertions = FREE_INIT_LEN * 2 + 10;
	ClientInfo *info;
	for (size_t i = 0; i < insertions; i++) {
		client_map_new_entry(&cm, i, &info);
	}

	/* Inserting doesn't affect the free cap/len. */
	cr_assert(eq(sz, cm.free_cap, FREE_INIT_LEN));
	cr_assert(eq(sz, cm.free_len, 0));

	for (size_t i = 0; i < insertions; i++) {
		client_map_delete(&cm, i);
	}

	cr_assert(eq(sz, cm.free_cap, FREE_INIT_LEN * 4));
	cr_assert(eq(sz, cm.free_len, insertions));
}
