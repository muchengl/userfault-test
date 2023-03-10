CCFLAGS := -pthread

server: server.c
	cc $(CCFLAGS) server.c -o server

client: client.c
	cc $(CCFLAGS) client.c -o client


server_udp: server_udp.c
	cc $(CCFLAGS) server_udp.c -o server_udp

client_udp: client_udp.c
	cc $(CCFLAGS) client_udp.c -o client_udp

client_udp_time_test: client_udp_time_test.c
	cc $(CCFLAGS) client_udp_time_test.c -o client_udp_time_test


