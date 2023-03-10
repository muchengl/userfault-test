#include <err.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <malloc.h>


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


static int * init_page(int pagenum)
{

    int page_size = sysconf(_SC_PAGE_SIZE); 
    int *p=(int *)malloc( pagenum * page_size ); 


    for(int i=0;i<=pagenum;i++){
        snprintf((char *) p+page_size*i, page_size, "Request #%d\n", i);
    }

    return p;

}

/*
 * This function is flaky. We could properly check write size, write
 * failures, etc...or even use SOCK_SEQPACKET for proper message boundaries.
 * But this isn't within the scope of the code.
 */
static void handle_connection(int connection_fd,int *p)
{
 
    struct sockaddr_in caddr;
    int len = sizeof(caddr);

    // 等待page请求
    int idx=0;
    int read = chk_sys(recvfrom(connection_fd, &idx, sizeof(idx),0,(struct sockaddr*)&caddr,&len));

    printf("Received request from client. Page id=%d\n",idx); 

    int page_size = sysconf(_SC_PAGE_SIZE);
    int *pp= p+page_size*idx/4;
    

    chk_sys(sendto(connection_fd, pp,page_size,0,(struct sockaddr*)&caddr,sizeof(caddr)));

}

int main(int argc, char *argv[])
{
    struct sockaddr_in server_addr, client_addr;
    int socket_fd, connection_fd;
    int err;

    int *p=init_page(24999);

    //socket_fd = chk_sys(socket(AF_INET, SOCK_STREAM, 0));
    socket_fd = socket(AF_INET,SOCK_DGRAM,0);

    // chk_sys(setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT,
    //             &(int){1}, sizeof(int)));

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(SERVER_PORT);
    server_addr.sin_addr.s_addr = INADDR_ANY;

    chk_sys(bind(socket_fd, (struct sockaddr *) &server_addr,
                sizeof(server_addr)));

    // chk_sys(listen(socket_fd, SOMAXCONN));


    // connection_fd = accept(socket_fd, (struct sockaddr *) &client_addr,
    //             &(unsigned int){sizeof(client_addr)});

    printf("Accepting inbound connections\n");
    for (;;) {
        handle_connection(socket_fd,p);
    }


    chk_sys(close(connection_fd));

    //free(page.data);
    return EXIT_SUCCESS;
}
