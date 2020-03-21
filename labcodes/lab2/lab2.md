# ucore lab2 实验报告

王焱 计73 2017050024

## 实验目的

- 理解基于段页式内存地址的转换机制
- 理解页表的建立和使用方法
- 理解物理内存的管理方法

## 实验内容

本次实验包含三个部分。首先了解如何发现系统中的物理内存；然后了解如何建立对物理内存的初步管理，即了解连续物理内存管理；最后了解页表相关的操作，即如何建立页表来实现虚拟内存到物理内存之间的映射，对段页式内存管理机制有一个比较全面的了解。本实验里面实现的内存管理还是非常基本的，并没有涉及到对实际机器的优化，比如针对 cache 的优化等。

### 练习 1：实现 first-fit 连续物理内存分配算法

- 首先first fit分配策略的思想是，在遍历空闲块列表时将找到的第一个满足需求的块拿来进行分配，而通常为了在高地址保留较大的空闲块，将空闲块列表按照地址由低到高排列。

- 阅读代码了解ucore中内存管理主要依赖的框架pmm_manager（包含一系列函数指针）和初始化内存管理的流程。

    pmm_manager中主要包含的函数有：
        init: 初始化用来管理内存的数据结构。
        init_memmap：将一个内存块（包含连续的page）插入到空闲块链表的某个位置。
        alloc_pages：从空闲块链表中取出一定数量的页面，进行分配。
        free_pages：释放一定数量的页面，重新添加到空闲块链表。

    初始化内存管理的流程：主要在pmm.c/pmm_init()中，由kern_init()调用。
    ```c
    init_pmm_manager();               //将pmm指定为default_pmm_manager，并调用init()进行初始化
    page_init();                      //根据探测所得结果，初始化物理内存对应的Page结构，并将空闲内存加入管理
    check_alloc_page();               //对pmm检查，其中会调用alloc_pages()和free_pages()等
    check_pgdir();                    //检查页目录表
    ```

- 对default_pmm_manager加以修改
  
    **default_init**只是初始化一个节点首尾相连的双向链表，不做修改。在这里了解了ucore中使用的list结构，链表以list_entry为节点由节点包含的两个list_entry指针左右相连，各种不同的数据结构只要使用list-entry就可以组成该双向链表结构。

    **default_init_memmap**中将所有页清零，设置head页属性，加入free_list。其中first fit要求地址由低到高排列，并且page_init()中遍历顺序时地址由低到高，因此要保持顺序，需要修改插入链表的位置。每次插入到free_list的前面，实际上也就是插入到上一个插入块的后面，这样就保证了顺序。但实际上，初始化时只有一块插入，所以并不会体现出差异。

    **default_alloc_pages**中从free_list开始遍历，遇到满足所需页数的块则开始分配，将所剩的页重新管理，需要修改的有两处，一是将head页的PG_property位置1，表示其是一块空闲内存的head页；二是插入链表时，将剩下的页还插入到原来的节点位置，以保证顺序。

    **default_free_pages**遍历空闲块链表，进行合并操作，插入链表，修改的是插入链表，需要先遍历连链表按地址由低到高的维护原则找到该插入的位置，然后在插入。

- 可改进的地方

    由于first fit 本身并不复杂，目前并没有想到改进的方法。可能存在的问题也许是每次遍历链表时的开销比较大。


### 练习 2：实现寻找虚拟地址对应的页表项

- 设计实现过程
  
    首先明确一点，页目录和页表中存储的都是物理地址，需要使用KADDR()进行转换，例如从页目录获取到的是页表的物理地址，转换成虚拟地址后才可以访问对应的页表。

    具体流程：首先由提供的参数 pgdir 和 la 利用PDX 可以获取到 pde ,再使用 PDE_ADDR 可以获取到页表的虚拟地址， 最后利用 PTX(la) 可以获取到 pte . 当pde不存在并且需要创建时，分配一页内存,将该页的物理地址填入对应的pde,并设置相关标志位。

- 请描述页目录项（Page Directory Entry）和页表项（Page Table Entry）中每个组成部分的含义以及对ucore而言的潜在用处。
  
    包含4KB页面首地址的物理地址和12位的flags。由于页对齐，首地址的低12位为零，所以可以用低12位作为flags。将pde、pte的低12位清零即可获得页面的物理地址，12位的flags用来描述该页信息。
    ```c
    /* page table/directory entry flags */
    #define PTE_P           0x001           // Present
    #define PTE_W           0x002           // Writeable
    #define PTE_U           0x004           // User
    #define PTE_PWT         0x008           // Write-Through
    #define PTE_PCD         0x010           // Cache-Disable
    #define PTE_A           0x020           // Accessed
    #define PTE_D           0x040           // Dirty
    #define PTE_PS          0x080           // Page Size
    #define PTE_MBZ         0x180           // Bits must be zero
    #define PTE_AVAIL       0xE00           // Available for software use
                                // The PTE_AVAIL bits aren't used by the kernel or interpreted by the
                                // hardware, so user processes are allowed to set them arbitrarily.
    ```

- 如果 ucore 执行过程中访问内存，出现了页访问异常，请问硬件要做哪些事情？

    将发生错误的线性地址保存在cr2寄存器中；
    在中断栈中依次压入EFLAGS，CS, EIP，以及页访问异常码error code;
    根据中断描述符表查询到对应page fault的ISR，跳转到对应的ISR处执行；

### 练习 3：释放某虚地址所在的页并取消对应二级页表项的映射

- 设计实现过程
  
    首先需要找到该虚拟地址所在页帧对应的Page结构，减少该物理页的引用计数，当引用计数为零时，即没有任何虚拟页指向该物理页帧，调用free_page 释放该页。然后将pte置零，表明该映射不存在。最后刷新TLB，保证TLB中的缓存不会有错误的映射关系。
  
- 数据结构 Page 的全局变量（其实是一个数组）的每一项与页表中的页目录项和页表项有无对应关系？如果有，其对应关系是啥？

    存在对应关系，由于页目录项和页表项中存的是物理页的物理地址，因此可以通过这个物理地址来获取到对应到的Page数组的对应项，
    将物理地址除以页的大小，可以得到是第几个页，然后乘上一个Page结构的大小可以获得偏移量，最后加上Page数组的基地址便可以得到对应Page项的地址。

- 如果希望虚拟地址与物理地址相等，则需要如何修改 lab2，完成此事？ 鼓励通过编程来具体完成这个问题

    在完全启动了ucore之后，虚拟地址和线性地址相等，都等于物理地址加上0xc0000000，如果需要虚拟地址和物理地址相等，可以考虑更新gdt，更新段映射，使得virtual address = linear address - 0xc0000000，这样的话就可以实现virtual address = physical address 。

## 实验小结

- 参考答案分析对比
        
    练习1中，alloc时，对于剩下的pages，答案是直接插在freelist后面，修改为插在原来的page的位置，然后再删掉原来的page。
    
    练习2和练习3与答案基本相同。

- 实验中重要知识点与其对应的OS原理
    - first fit 分配策略
    - 分页机制
    - 虚拟存储和二级页表

- 未出现的OS知识点
    - 非连续内存分配
    - 页面置换