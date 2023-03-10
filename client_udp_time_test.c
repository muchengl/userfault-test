#include <fcntl.h>
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
#include<signal.h>

#include "stdlib.h"
#include "time.h"


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


long i = 10000000L; 
clock_t start, finish; 
double Total_time; 

double network_time=0;
double faulting_thread_time=0;
double handle_pagefault_time=0;
double wait_time=0;



struct faulting_thread_args { 
    uint8_t *playground; 
    int playground_size; 
}; 

static int *__faulting_thread(struct faulting_thread_args *args)
{
    int dev_null = chk_sys(open("/dev/null", O_WRONLY)); 

    
    clock_t start=clock(); 

    for (int i=0; i < args->playground_size; i+=page_size) { 
        /* Trigger a page fault by reading from playground */
        int x = args->playground[i] + 2; 
        write(dev_null, &x, sizeof(x)); 

        uint64_t page_boundary = ((uint64_t) &args->playground[i]) & ~(page_size - 1); 
        if ((uint64_t) &args->playground[i] == page_boundary) { 
            //printf("faulted page contents: %s\n", (char *) page_boundary); 
        }
    }

    clock_t finish=clock(); 
    faulting_thread_time+=(double)(finish-start) / CLOCKS_PER_SEC;

    chk_sys(close(dev_null)); 
    return NULL; 
}

static void *faulting_thread(void *args)
{
    return (void *) __faulting_thread((struct faulting_thread_args *) args); 
}



int socket_fd;
struct sockaddr_in server_addr; 
int page_idx=0;

static void init_network(){
    //socket_fd = chk_sys(socket(AF_INET, SOCK_STREAM, 0)); 
    socket_fd = socket(AF_INET,SOCK_DGRAM,0);

    chk_sys(setsockopt(socket_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT,
                &(int){1}, sizeof(int)));

    memset(&server_addr, 0, sizeof(server_addr)); 
    server_addr.sin_family = AF_INET; 
    server_addr.sin_addr.s_addr = inet_addr(SERVER_IP); 
    server_addr.sin_port = htons(SERVER_PORT); 


}



static void fetch_remote_page(uint8_t *page)
{
    
    clock_t start=clock();  

    // Which page is needed
    chk_sys(sendto(socket_fd, &page_idx, sizeof(page_idx),0,(struct sockaddr*)&server_addr,sizeof(server_addr)));
    page_idx++;

    int len=sizeof(server_addr);

    // Read from remote node
    int bytes_read = chk_sys(recvfrom(socket_fd, page, page_size,0,(struct sockaddr*)&server_addr,&len)); 
    if (bytes_read != page_size) { 
        /* Fail, etc. */
        fprintf(stderr, "Unable to read enough bytes from server\n");
        exit(EXIT_FAILURE);
    }

    clock_t finish=clock(); 
    network_time+=(double)(finish-start) / CLOCKS_PER_SEC;

    //chk_sys(close(socket_fd)); 
}

static void handle_pagefault(int uffd, struct uffd_msg *uffd_msg)
{
   

    if (uffd_msg->event != UFFD_EVENT_PAGEFAULT) {
        fprintf(stderr, "Unknown event on userfaultfd\n");
        exit(EXIT_FAILURE);
    }

    // printf("pagefault detected at address=0x%" PRIx64 "\n", 
    //         (uint64_t) uffd_msg->arg.pagefault.address); 

    //printf("%d\n",page_idx);

    uint8_t *page = malloc(page_size); 


    fetch_remote_page(page); 


    clock_t start=clock(); 

    struct uffdio_copy uffdio_copy; 
    memset(&uffdio_copy, 0, sizeof(uffdio_copy)); 
    uffdio_copy.src = (uint64_t) page; 
    /* align destination to the enclosing page of the faulted address */
    uffdio_copy.dst = uffd_msg->arg.pagefault.address & ~(page_size - 1); 
    uffdio_copy.len = page_size; 
    chk_sys(ioctl(uffd, UFFDIO_COPY, &uffdio_copy)); 
    if (uffdio_copy.copy != page_size) { 
        // fprintf(stderr, "pagefault data only filled %" PRId64 "bytes \n",
        //         uffdio_copy.copy); 
        exit(EXIT_FAILURE); 
    }

    free(page); 

    clock_t finish=clock(); 
    handle_pagefault_time+=(double)(finish-start) / CLOCKS_PER_SEC;
}

static int init_userfaultfd(uint8_t *addr, int len)
{

    int uffd = chk_sys(syscall(SYS_userfaultfd, O_CLOEXEC | O_NONBLOCK)); 

    /* initialize userfaultfd, check for requested features */ 
    struct uffdio_api uffdio_api;
    memset(&uffdio_api, 0, sizeof(uffdio_api)); 
    uffdio_api.api = UFFD_API; 
    //uffdio_api.features=UFFD_FEATURE_SIGBUS;
    
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
    
    double ans=0;
    int n=10;
    for(int i=0;i<n;i++) {

    //sleep(3);
    page_idx=0;
    network_time=0;
    faulting_thread_time=0;
    handle_pagefault_time=0;
    wait_time=0;

    page_size = sysconf(_SC_PAGE_SIZE); 

    /* 
     * Intercepting page faults for this program's _instructions_ would
     * immediately terminate the program. Instead, we restrict userfaultfd
     * to a small memory range. 
     */
    
    int pagenum=24999;
    int playground_size = pagenum * page_size; 


    // Linux supports MAP_ANONYMOUS; don't need to map /dev/zero like in BSD. 
    uint8_t *playground = mmap(NULL, playground_size, 
            PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0); 
    if (playground == MAP_FAILED) { 
        perror(NULL); 
        exit(EXIT_FAILURE);
    }

    int uffd = init_userfaultfd(playground, playground_size); 

    init_network();


    pthread_t worker; 
    struct faulting_thread_args faulting_thread_args = { 
        .playground = playground, .playground_size = playground_size 
    }; 
    int err = pthread_create(&worker, NULL, faulting_thread, 
            (void *) &faulting_thread_args); 
    if (err) { 
        fprintf(stderr, "%s:%d: %s\n", __func__, __LINE__, strerror(err)); 
        exit(EXIT_FAILURE);
    }



    //long i = 10000000L; 
    clock_t start, finish; 
    double Total_time; 
    //printf( "Time to do %ld empty loops is ", i ); 
    start = clock(); 


    
    /* Handle page faults */
    for (;;) { 
        clock_t a1=clock();

        struct pollfd pollfd;
        memset(&pollfd, 0, sizeof(pollfd)); 
        pollfd.fd = uffd; 
        pollfd.events = POLLIN; 
        poll(&pollfd, 1, -1); 

        struct uffd_msg uffd_msg;
        
        read(uffd, &uffd_msg, sizeof(uffd_msg));

        clock_t a2=clock(); 
        wait_time+=(double)(a2-a1) / CLOCKS_PER_SEC;

        handle_pagefault(uffd, &uffd_msg); 


        if(page_idx==pagenum){
            finish = clock(); 
            Total_time = (double)(finish-start) / CLOCKS_PER_SEC; 
            printf("====================\n");
            printf("Page size: %d B  Page number: %d \n",page_size,pagenum);
            printf("Memory: %d Mb\n",page_size*pagenum/(1000*1000));
            printf( "%f Seconds\n", Total_time); 

            printf("Network time: %f\n",network_time);
            printf("Fault thread time: %f\n",faulting_thread_time);
            printf("Handle pagefault time: %f\n",handle_pagefault_time);
            printf("Wait time: %f\n",wait_time);
            printf("Total time :%f\n",network_time+faulting_thread_time+handle_pagefault_time+wait_time);
            ans+=network_time+faulting_thread_time+handle_pagefault_time+wait_time;
            printf("====================\n\n");
            break;
        }
       
    }
    close(socket_fd);
    chk_sys(munmap(playground, playground_size)); 
    }

    printf("Ans:%f\n",ans);
    printf("Ans_a:%f\n",ans/n);

    return EXIT_SUCCESS;
}
