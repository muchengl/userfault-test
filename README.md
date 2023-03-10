# userfault-test

Test the user space page fault. Using userfalutfd.

**Server端**使用malloc申请内存，并进行初始化。**Client端**使用mmap初始化内存，将fd参数设置为-1，从而获得大量未被映射的内存。并将这些内存标记为UFFD_EVENT_PAGEFAULT，即内核应将该内存的Page fault交给用户处理。Client端开启一个fault触发线程，不断按顺序access page，触发page fault。client端监听描述符uffd，获取内核传来的user fault信息，并通过udp协议，从server端获取相应的page，进行内存初始化。

在Ununtu 20.01系统进行模拟测试，数据传输方式为UDP，每组测量进行20次，取CPU运行时间的平均值。可以发现，远程拷贝总时间与页数呈正比。在拷贝内存为100M时（25000页），需要约0.98秒的时间。

| 名称 | 含义 |
| :----:| :---: |
| Fault process time | Fault触发线程的总运行时间，也就是负载代码的实际运行时间 |
| Network time | 使用UDP获取远程页的时间 |
| Handle time | 获取到远程页后，对原创页中的数据进行复制的时间 |
| IO time | 等待内核通过fd传输User Fault相关信息的时间 |
| Init time | 代码初始化时间 |

经计算：Fault process time ≈ Network + Handle + IO + Init

![2023-03-10 22.05.51](https://raw.githubusercontent.com/muchengl/pic_storage/main/uPic/%E6%88%AA%E5%B1%8F2023-03-10%2022.05.51.png)

（由于条件有限，本测试很不完善，仅具有参考意义：1.是在虚拟机上运行，受到系统调度影响，测试结果很不稳定。2.未来预期使用xquic而非UDP，因此此测试不能完全代表xquic的性能。3.本测试在两台虚拟机间进行，与真实网络环境有很大差异。）
