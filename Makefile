CC := gcc
CFLAGS := -Wall -Wextra -ggdb -MD -MP
LDLIBS := -luring
TEST_DIR := tests
BUILD_DIR := build

SERVER_SRCS := main.c utils.c op.c op_pool.c client_map.c slab.c protocol.c server.c
SERVER_OBJS := $(patsubst %.c,$(BUILD_DIR)/%.o,$(SERVER_SRCS))

CLIENT_SRCS := client.c utils.c
CLIENT_OBJS := $(patsubst %.c,$(BUILD_DIR)/%.o,$(CLIENT_SRCS))

TEST_PROT_SRCS := test_protocol.c utils.c protocol.c
TEST_PROT_OBJS := $(patsubst %.c,$(BUILD_DIR)/%.o,$(TEST_PROT_SRCS))

TEST_TARGET_SRCS := utils.c cid_set.c groups.c slab.c client_map.c op_pool.c op.c
TEST_TARGET_OBJS := $(patsubst %.c,$(BUILD_DIR)/%.o,$(TEST_TARGET_SRCS))

TEST_SRCS := $(wildcard $(TEST_DIR)/*.c)
TEST_OBJS := $(TEST_SRCS:$(TEST_DIR)/%.c=$(BUILD_DIR)/%.o)

SERVER_BIN := server
CLIENT_BIN := client
TEST_PROT_BIN := test_protocol
TEST_BIN := test_runner

.PHONY: build-server build-client run-tests clean

$(BUILD_DIR):
	@mkdir -p $(BUILD_DIR)

build-server: $(SERVER_BIN)

$(SERVER_BIN): $(SERVER_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

build-client: $(CLIENT_BIN)

$(CLIENT_BIN): $(CLIENT_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

build-test-protocol: $(TEST_PROT_BIN)

$(TEST_PROT_BIN): $(TEST_PROT_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDLIBS)

run-tests: $(TEST_BIN)
	./$(TEST_BIN)

$(TEST_BIN): $(TEST_TARGET_OBJS) $(TEST_OBJS)
	$(CC) $(CFLAGS) $^ -o $@ -lcriterion

$(BUILD_DIR)/%.o: %.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD_DIR)/%.o: $(TEST_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@ -lcriterion

clean:
	rm -rf $(BUILD_DIR) $(SERVER_BIN) $(CLIENT_BIN) $(TEST_BIN)

-include $(wildcard $(BUILD_DIR)/*.d)
