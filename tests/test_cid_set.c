#include "../cid_set.h"
#include <criterion/criterion.h>
#include <criterion/new/assert.h>
#include <stdio.h>

Test(cid_set, operations) {
  struct cid_set set;
  cid_set_init(&set);

  size_t old_cap = set.cap;
  cr_assert(set.len == 0);
  for (size_t i = 0; i < old_cap; i++) {
    cid_set_insert(&set, i);
  }

  cr_assert(eq(u64, set.len, old_cap));
  cr_assert(eq(u64, set.cap, 2 * old_cap));
  for (size_t i = 0; i < old_cap; i++) {
    cr_assert(cid_set_exists(&set, i));
  }

  cr_assert(not(cid_set_exists(&set, 1000)));
}
