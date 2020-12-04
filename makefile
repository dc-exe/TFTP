CC=gcc

myclient: client.c myserver
	$(CC) client.c -o client

myserver: server.c
	$(CC) server.c -o server