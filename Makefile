CC := gcc
CFLAGS := -Wall -Wextra -ggdb
LDFLAGS := -luring

# === Source files ===
SERVER_SRCS := main.c op.c pool.c utils.c
CLIENT_SRCS := client.c utils.c

# === Object files ===
SERVER_OBJS := $(SERVER_SRCS:.c=.o)
CLIENT_OBJS := $(CLIENT_SRCS:.c=.o)

# === Targets ===
SERVER_BIN := chat
CLIENT_BIN := client

# === Server build ===
build: $(SERVER_BIN)

$(SERVER_BIN): $(SERVER_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# === Client build ===
build-client: $(CLIENT_BIN)

$(CLIENT_BIN): $(CLIENT_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf *.o $(SERVER_BIN) $(CLIENT_BIN)
