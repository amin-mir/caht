#include <stddef.h>

struct slab {
	size_t cap;
	size_t len;
	size_t buf_len;
	char **buffers;
};

void slab_init_cap(struct slab *s, size_t buf_len, size_t cap);
void slab_init(struct slab *s, size_t buf_len);
void slab_deinit(struct slab *s);
char *slab_get(struct slab *s);
void slab_put(struct slab *s, char *buf);
