#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>

#define SERVER_PORT 8888

#define chk_sys(x) __chk_sys((x), __func__, __LINE__)
static int __chk_sys(int ret, const char *func, int line)
{
    if (ret == -1) {
        perror(NULL);
        exit(EXIT_FAILURE);
    }
    return ret;
}

static struct {
    size_t size;
    uint8_t *data;
    int index;
} page;

static void init_page()
{
    page.size = sysconf(_SC_PAGE_SIZE);
    page.data = calloc(sizeof(*page.data), page.size);
    if (page.data == NULL) {
         perror(NULL);
         exit(EXIT_FAILURE);
    }
    page.index = 0;
}

/*
 * This function is flaky. We could properly check write size, write
 * failures, etc...or even use SOCK_SEQPACKET for proper message boundaries.
 * But this isn't within the scope of the code.
 */
static void handle_connection(int connection_fd)
{
    snprintf((char *) page.data, page.size, "Request #%d\n", page.index);
    page.index++;

    chk_sys(write(connection_fd, page.data, page.size));
    chk_sys(close(connection_fd));
}

int main(int argc, char *argv[])
{
    struct sockaddr_in server_addr, client_addr;
    int socket_fd, connection_fd;
    int err;

    init_page();

    socket_fd = chk_sys(socket(AF_INET, SOCK_STREAM, 0));
    chk_sys(setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT,
                &(int){1}, sizeof(int)));

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);
    server_addr.sin_addr.s_addr = INADDR_ANY;
    chk_sys(bind(socket_fd, (struct sockaddr *) &server_addr,
                sizeof(server_addr)));

    chk_sys(listen(socket_fd, SOMAXCONN));

    printf("Accepting inbound connections\n");
    for (;;) {
        connection_fd = accept(socket_fd, (struct sockaddr *) &client_addr,
                &(unsigned int){sizeof(client_addr)});
        handle_connection(connection_fd);
    }

    free(page.data);
    return EXIT_SUCCESS;
}
