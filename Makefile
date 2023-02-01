CCFLAGS := -pthread

server: server.c
	cc $(CCFLAGS) server.c -o server

client: client.c
	cc $(CCFLAGS) client.c -o client
