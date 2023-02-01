#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/mman.h>
#include <netinet/in.h>
#include <unistd.h>
#include <sys/syscall.h>
#include <linux/userfaultfd.h>
#include <sys/ioctl.h>
#include <errno.h>
#include <stdbool.h>
#include <poll.h>
#include <inttypes.h>
#include <arpa/inet.h> 

#define SERVER_IP "127.0.0.1"
#define SERVER_PORT 8888
static int page_size; 

#define chk_sys(x) __chk_sys((x), __func__, __LINE__)
static int __chk_sys(int ret, const char *func, int line)
{
    if (ret == -1) {
        fprintf(stderr, "%s:%d: %s\n", func, line, strerror(errno)); 
        exit(EXIT_FAILURE);
    }
    return ret;
}

struct faulting_thread_args { 
    uint8_t *playground; 
    int playground_size; 
}; 

static int *__faulting_thread(struct faulting_thread_args *args)
{
    for (int i = args->playground_size-1; i >= 0; i--) { 
        args->playground[i] = (uint8_t) random(); 
    }
    return NULL; 
}

static void *faulting_thread(void *args)
{
    return (void *) __faulting_thread((struct faulting_thread_args *) args); 
}


static void fetch_remote_page(uint8_t *page)
{
    int socket_fd = chk_sys(socket(AF_INET, SOCK_STREAM, 0)); 
    chk_sys(setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT,
                &(int){1}, sizeof(int)));

    struct sockaddr_in server_addr; 
    memset(&server_addr, 0, sizeof(server_addr)); 
    server_addr.sin_family = AF_INET; 
    server_addr.sin_addr.s_addr = inet_addr(SERVER_IP); 
    server_addr.sin_port = htons(SERVER_PORT); 

    chk_sys(connect(socket_fd, (struct sockaddr *) &server_addr,
                sizeof(server_addr))); 
    int bytes_read = chk_sys(read(socket_fd, page, page_size)); 
    if (bytes_read != page_size) { 
        // Yes, yes, I know this fails if there's TCP fragmentation. 
        fprintf(stderr, "Unable to read enough bytes from server\n");
        exit(EXIT_FAILURE);
    }

    chk_sys(close(socket_fd)); 
}

static void handle_pagefault(int uffd, struct uffd_msg *uffd_msg)
{
    if (uffd_msg->event != UFFD_EVENT_PAGEFAULT) {
        fprintf(stderr, "Unknown event on userfaultfd\n");
        exit(EXIT_FAILURE);
    }

    printf("pagefault detected at address=0x%" PRIx64 "\n", 
            (uint64_t) uffd_msg->arg.pagefault.address); 

    uint8_t *page = malloc(page_size); 

    fetch_remote_page(page); 

    struct uffdio_copy uffdio_copy; 
    memset(&uffdio_copy, 0, sizeof(uffdio_copy)); 
    uffdio_copy.src = (uint64_t) page; 
    /* align destination to the enclosing page of the faulted address */
    uffdio_copy.dst = uffd_msg->arg.pagefault.address & ~(page_size - 1); 
    uffdio_copy.len = page_size; 
    chk_sys(ioctl(uffd, UFFDIO_COPY, &uffdio_copy)); 
    if (uffdio_copy.copy != page_size) { 
        fprintf(stderr, "pagefault data only filled %" PRId64 "bytes \n",
                uffdio_copy.copy); 
        exit(EXIT_FAILURE); 
    }

    free(page); 
}

static int init_userfaultfd(uint8_t *addr, int len)
{
    /* TODO: Do we want to handle minor page faults too? */
    int uffd = chk_sys(syscall(SYS_userfaultfd, UFFD_USER_MODE_ONLY)); 

    /* initialize userfaultfd, check for requested features */ 
    struct uffdio_api uffdio_api;
    memset(&uffdio_api, 0, sizeof(uffdio_api)); 
    uffdio_api.api = UFFD_API; 
    chk_sys(ioctl(uffd, UFFDIO_API, &uffdio_api)); 

    /* register userfaultfd handler for addr region */ 
    struct uffdio_register uffdio_register;
    memset(&uffdio_register, 0, sizeof(uffdio_register)); 
    uffdio_register.range.start = (unsigned long long) addr; 
    uffdio_register.range.len = len; 
    uffdio_register.mode = UFFDIO_REGISTER_MODE_MISSING; 
    chk_sys(ioctl(uffd, UFFDIO_REGISTER, &uffdio_register)); 

    return uffd; 
}

int main(int argc, char *argv[])
{
    int err; 

    /* 
     * Intercepting page faults for this program's _instructions_ would
     * immediately terminate the program. Instead, we restrict userfaultfd
     * to a small memory range. 
     */
    page_size = sysconf(_SC_PAGE_SIZE); 
    int playground_size = 1000 * page_size; 
    // Linux supports MAP_ANONYMOUS; don't need to map /dev/zero like in BSD. 
    uint8_t *playground = mmap(NULL, playground_size, 
            PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0); 
    if (playground == MAP_FAILED) { 
        perror(NULL); 
        exit(EXIT_FAILURE);
    }

    int uffd = init_userfaultfd(playground, playground_size); 

    pthread_t worker; 
    struct faulting_thread_args faulting_thread_args = { 
        .playground = playground, .playground_size = playground_size 
    }; 
    if ((err = pthread_create(&worker, NULL, faulting_thread, 
                    (void *) &faulting_thread_args))) { 
        fprintf(stderr, "%s:%d: %s\n", __func__, __LINE__, strerror(err)); 
        exit(EXIT_FAILURE);
    }

    /* Handle page faults */
    for (;;) { 
        struct pollfd pollfd;
        memset(&pollfd, 0, sizeof(pollfd)); 
        pollfd.fd = uffd; 
        pollfd.events = POLLIN; 
        chk_sys(poll(&pollfd, 1, -1)); 

        struct uffd_msg uffd_msg;
        if (chk_sys(read(uffd, &uffd_msg, sizeof(uffd_msg))) == 0) { 
            fprintf(stderr, "no messages read from uffd\n"); 
            exit(EXIT_FAILURE); 
        }

        handle_pagefault(uffd, &uffd_msg); 
    }

    chk_sys(munmap(playground, playground_size)); 
    return EXIT_SUCCESS;
}
