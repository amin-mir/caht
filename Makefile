build:
	gcc -Wall -Wextra -ggdb -o chat main.c op.c utils.c pool.c -luring

build-client:
	gcc -Wall -Wextra -ggdb -o client client.c -luring
