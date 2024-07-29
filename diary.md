[课程网站](https://pdos.csail.mit.edu/6.828/2023/schedule.html)

[课程讲稿翻译](https://mit-public-courses-cn-translatio.gitbook.io/mit6-s081)

[xv6-book中文版](https://github.com/FrankZn/xv6-riscv-book-Chinese)

待实现的optional challenge：(2024.4.13)

- [ ] lab3 pgtbl:取消user space第0页的映射(让null指针出错)
- [ ] lab4 trap：在backtrace中打印函数名和代码行号



可进一步思考的问题：

1. [linux中的ptrace(如果让xv6支持信号，或许能实现简化版ptrace？)](#ptrace)
2. [vdso原理](#vdso)
3. [linux如何处理signal的handler在用户态和内核态间的切换](#signal)
4. [linux中的cow](#linux_cow)



2023.9.14

apt安装依赖时版本报错，使用``aptitude install`解决，`make qemu`正常运行

做lab1时无法切换到util分支，clone实验版本xv6（之前clone的是成品？），`make qemu`正常运行



2023.9.21

没找到sleep系统调用的实现，在尝试若干天后直接调用sleep(int)通过测试



2023.9.28

完成pingpong后无法调用，提示`exec pingpong failed`



<p name="pingpong_bug">2023.10.2</p>

`make clean`后运行成功，多运行几次pingpong出现过child没输出的情况，添加对read/write返回值的判断再运行正常，且不需要clean（玄学）



### utils

#### find

2024.3.28

在一个窗口运行`make qemu-gdb`,在另一个窗口运行`gdb-multiarch`进入调试。make时会在工作目录下生成.gdbinit。在运行中使用`file user/_ls`加载ls准备调试，在打断点暂停单步调试时，用`n`或`s`均会在某个时候(貌似只会在有系统调用的位置)出现<a name="ls_bug">``Cannot find bounds of current function`</a>且不总是能发生，但`c`后能正常运行。用`info threads`看到默认在3个线程下工作，但另外2个实际上没运行，<span name="thread_num">在makefile中将CPUS变量</span>改为1，出现上面原因的次数减少但仍有。

当gdb-multiarch刚连接到xv6后用file加载完user/_ls，在`45: switch(st.type){`打完断点用c运行，明明没执行ls但会触发断点，且访问到非法地址，并进入`Cannot find bounds of current function`，再c能继续正常运行;在`34: if((fd = open(path, O_RDONLY)) < 0){`打断点又能执行ls后才触发

循环跳出后忘记close(fd)资源耗尽，导致open时返回-1；搜索过程用全局变量保存路径，在递归函数中修改全局变量导致返回后原路径被改变

<img src="diary_img/3.png" style="zoom:67%;" />

#### xargs

2024.3.29

read会读取\n,argv指向的字符串要以‘\0’结尾,需要把从命令行读取的用空格分开的字符改为用‘\0’分开。

`echo aaa | xargs echo bbb ccc`测试正常，用grep测试报错

![](diary_img/1.png)

标准输出肯定没关，否则上述用echo测试也会报错。需要看看管道的实现

后续测试还出现无法open 2

![](diary_img/6.png)



2024.4.1

打印出传递给grep的参数，看到ls不仅仅输出文件名导致参数过多，用ls测试不合适

单步调试xargs.c，打完断点后执行混乱，仅按换行都会hit断点，应该是调试文件加载有问题，执行内核态代码命中了用户态断点（xv6调试怎么有一堆奇怪问题？？）

用find通过管道给grep传参，看到只有第一次调用grep参数正确(find每输出一行就exec一次grep)，后续的argv只有“grep”一个参数，可能是字符串处理出错

![](diary_img/8.png)

用一个buf保存xargs后面的原始参数，每读取一行后直接把参数往buf后追加，参数间的空格用‘\0’代替，读下一行时清空上一行读入的追加参数，但此时原始参数间的空格被替换成了‘\0’,导致在划分argv时识别完第一个参数遇到‘\0’退出循环。改为用两个buf，每次清空后一个buf，把原始参数的buf复制到新buf，对新buf重新分割argv



2024.3.31

准备组会时复现调试ls出现的[bug](#ls_bug),用`info thread`看到出现bug时ls被切换到其他thread，用`thread <thread-id>`切换到当前thread即可继续调试，但为了方便，还是将[thread数改为1](#thread_num)

![](diary_img/2.png)



#### pingpong

看到之前的[日记](#pingpong_bug)发现pingpong有bug，测了一下仍然有。在父进程用wait后能正常输出，需要去看看printf源码

![](diary_img/4.png)

![](diary_img/5.png)

把管道相关代码删除后能正常输出，看来是管道的问题



2024.4.1

父子进程能同时读写管道，父进程write完立刻read管道中的数据，子进程在read处阻塞，所以只有父进程正常退出并输出，而子进程一直在等数据。pipe只有读写的某一端close并执行另一的操作才会收到EOF或SIGPIPE，注意这里父子进程共打开2个读端2个写端。打开两个管道即可解决。



### syscall

#### trace

2024.4.1

在kernel/trap.c:usertrap(void)中调用syscall

p->trapframe->a7保存系统调用号



2024.4.2

sstatus的SPP(1L << 8)位为0，表明之前的模式为user

scause=0xd，含义为Load page fault

在fork中加入mask是为了通过对子进程的trace

exec只替换当前进程执行的程序，所以proc->trace_mask不会变

> <span name="ptrace">linux中的strace和ptrace是如何实现的？</span>可参考[这里](https://zhuanlan.zhihu.com/p/441291330)

#### sysinfo

2024.4.4

xv6内存管理采用栈式链表，每个结点管理1页，结点数据结构保存在页中，每次分配回收的单位都是页，所以初始化后有$\frac{total\_mem}{page\_size}$个结点



### pgtbl

#### speed up system calls

2024.4.6

直接在kernel/proc.c:proc_pagetable中用mappages把p->pid映射到USYSCALL，scause报错原因为`Store/AMO access fault`，发现应该传参&p->pid，而不是p->pid，因为是要把p->pid的地址映射到用户空间，而p->pid的值通常较小，把其当做物理地址是映射到IO区。

![](diary_img/9.png)

传&p->pid后不报错，但syscall->pid为0，因为mappages会将&p->pid舍入到对齐页面，即只能将页边界映射到页边界，&p->pid距离其页边界有偏移量，下图中，偏移量为0x210，其中0x3fffffd000为syscall在用户空间的地址(地址固定)，walkaddr函数返回用户态下虚拟地址对应的物理地址。

![](diary_img/10.png)

在struct usyscall中把偏移量填充上，这样usyscall->pid可取到正确的pid

![](diary_img/11.png)

![](diary_img/12.png)

这种实现明显不行，参考其他人的做法，给进程再申请一个内核页(kernel/proc.c:allocproc)，该页映射到0x3fffffd000(kernel/proc.c:proc_pagetable)，struct proc有struct usyscall的指针指向该页起始地址，在创建proc时把pid写入到该页(kernel/proc.c:usyscall_init)

在freeproc中释放proc->usyscall指向的物理地址，在proc_freepagetable中释放映射proc->usyscall的页表

> 之前直接把pid映射到用户空间的做法能保证数据实时同步，但映射以页为单位，不可能用填充的方法做到同步，尤其是添加更多共享数据时
>
> <span name="vdso">linux中的vdso如何实现？似乎有周期更新的操作</span>

vm.c:uvmfree、vm.c:freewalk、proc.c:proc_freepagetable、proc.c:freeproc是什么关系？？

#### print a page table

2024.4.6

make grade无法通过，要看看是如何测试的，可能是格式问题

2024.4.7

测试用例的虚拟地址是固定的，物理地址可以不同，但没看懂哪里有问题

看别人的做法，是在print时多打了0x

![](diary_img/13.png)



#### Detect which pages have been accessed

![](diary_img/17.png)

2024.4.7

用walk遍历每个页的虚拟地址，从最低级pte获取PTE_A位，读取完后要将其清零

猜测PTE_A位只有在用户态下访问才会被置位，否则内核态每次查看就会置位

较简单，一次通过

2024.4.10

询问ai并回顾walk的执行过程，walk只是返回pte，并没有访问pte指向的页，所以PTE_A不会置位，在内核态下访问的页仍会置位

测试方法：在内核态中找到一个用户态下未被访问的页，执行walk前查看pte，执行后再查看pte，pte的PTE_A应该会置位



2024.4.7

尝试完成optional第二项：unmap用户进程的第0页，这样解引用空指针会报错

把user.ld中.text起始地址改为0x1000(4096)

在pgtbltest.c中测试解引用空指针，发现修改.text后$*(int*)0 == 0$,未发生异常

![](diary_img/14.png)

segment映射在exec.c中，关键函数应该为uvmalloc



2024.4.8

以/init调用exec(“sh”)为例，按理说在exec加载完sh的所有segment后调用uvmunmap取消第0页的映射即可，但exec最后要调用kernel/proc.c:proc_freepagetable释放init的页表，该页表的第0页没映射(调用exec(“init”)时释放了)，在释放时从vaddr=0开始，发现第0页无效，发生panic。在kernel/vm.c:uvmfree中，给uvmunmap的va形参传入PAGESZ，即从vaddr=PAGESZ开始释放

![](diary_img/15.png)

接上，proc->name==“proc”的进程调用exec(“init”)，但“proc”自身从第0页开始，不受user.ld影响，释放自身页表时uvmunmap出错，因为其只占1页，但从vaddr=PAGESZ开始释放，而第1页未分配，所以panic

![](diary_img/16.png)

调用fork时，kernel/vm.c:uvmcopy会复制父进程页表，由于第0页已经unmap，所以在循环中要从PGSIZE开始，否则第0页的pte无效会panic



### trap

![](diary_img/18.png)

![](diary_img/19.png)

2024.4.10

trap执行流程：ecall \==进入内核态\==> uservec(trampoline.s) \==跳转\==> usertrap(trap.c) \==调用\==> syscal(syscall.c)  \==返回到usertrap，调用(事实上不会再返回)\==> usertrapret(trap.c) \==调用(不会再返回)\==> userret(trampoline.s) \==> 回到用户态

ecall完成的工作：

* 从用户态切换到内核态
* 把ecall指令自己的地址保存到sepc中
* 跳转到stvec指向的地址(pc <- stvec)，也就是trampoline page中的第一条指令

ecall切换到内核态后，用的还是用户态页表，所以需要在用户态页表下执行一些使用内核态页表前的准备工作，这些工作就在trampoline page(代码页)中，trapframe page是数据页，保存用户态寄存器(包括当前cpu的编号)和跳转执行内核代码需要的一些地址(内核页表基址，内核栈地址，跳转入口)

uservec完成：

1. 保存31个通用寄存器
2. 从trapframe中加载kernel栈指针、hartid、内核页表
3. 跳转到usertrap



中断会在trap时被硬件关闭

2024.4.11

trapframe中的cpu编号是谁保存的？不应该是把当前cpu编号保存到trapframe中？trapframe->epc为啥不在trampoline中保存而要放到usertrap中保存？(有学生提问过，老师解释是都可以)

uservec中用到的sscratch就是个临时寄存器，用来保存a0的值，把a0空出来让其指向trapframe。在trapframe中保存完其余30个寄存器后(总共32个通用寄存器，x0/zero寄存器恒为0不保存)，用t0当做临时寄存器从sscratch中读出原来a0的值并store到trapframe。在跳转usertrap之前，a0始终指向trapframe

> 在fall 2023的代码中，**csrw sscratch, a0; li a0, TRAPFRAME**显式把TRAPFRAME加载到a0，而在之前的版本中，是用csrrw交换a0和sscratch，其中sscratch原来保存了TRAPFRAME

usertrap完成：

1. 设置stvec指向kernelvec，这样发生interrupt/exception时执行kernelvec，而不是uservec(只处理从用户态进入的trap)

2. 保存sepc + 4(sepc保存ecall的地址，在return时希望跳转到ecall的下一条指令)到trapframe

3. 开中断

4. 调用syscall

5. 调用usertrapret

usertrapret完成：

1. 关中断
2. 恢复stvec为uservec
3. 保存内核页表、kernel栈指针、usertrap地址、hartid到trapframe
4. 修改sstatus，使其sret时回到用户态并开中断
5. 跳转到userret

userret完成：

1. 切换到用户页表
2. 还原31个寄存器(a0的值已被修改为系统调用返回值，而不是原来的调用参数)
3. sret

sret完成的工作：

1. 切到用户态
2. pc <- sepc
3. 开中断



#### backtrace

2024.4.10

在sys_sleep中调用kernel/printf.c:backtrace输出的栈帧返回地址较大且只有1个，但在gdb下能看到有3层栈帧。通过gdb从sys_sleep开始调试汇编代码，发现在backtrace中读取fp的函数r_fp()出错，按照文档，r_fp()是inline的，但在lab开始前把makefile中的编译优化选项`-O`改成了`-O0`，关闭优化后inline无效，r_fp读取的是r_fp()函数栈帧中的fp，而不是backtrace的fp。因为r_fp()是leaf函数，不保存ra到栈，而只保存fp，所以循环中从fp-8读取的值不是ra而是上一个栈帧的fp；fp-16的位置没用到，读取后自然不是上一个栈帧的fp，所以退出循环。

开启`-O`后，backtrace中调用了printf，所以不是leaf函数，不然也是错的

上述bug可通过查看kernel/kernel.asm中backtrace相关的汇编代码发现

#### alarm

2024.4.11

在struct proc中添加ticks和handler，sys_sigalarm只需给这两个成员赋值。在kernel/trap.c:usertrap的时钟中断里ticks--。linux调用信号处理函数时的user/kernel切换如下：

![](diary_img/20.png)

在test0中，处理函数调用的sys_sigreturn无作用，即handler执行完后不会返回内核态(用户态主动进入内核态只能通过ecall)。

在进入内核态后，trapframe->epc是trap结束返回用户态的位置，把trapframe->epc赋值给trapframe->ra，再让trapframe->epc指向handler，这样返回用户态后会执行handler，并且在handler执行前，ra指向进入内核态前的位置，即在handler看来，原来的执行流没有被中断，而是调用了handler，handler执行完后跳到ra的位置，继续执行原来的代码。

注意：

1. 在这种方式下，从handler回到原来的应用程序不需要进入内核态。

2. 被中断函数已经保存了ra到栈上，所以经过handler返回后，ra的值已经改变但不影响(如果被中断的函数是leaf的，其只保存fp而不保存ra，这样很可能会出错？)

   > 经测试果然出错
   >
   > ```c
   > int main() {
   >     sigalarm(2, periodic);
   >     printf("%d\n", test0());
   > }
   > 
   > volatile static int count;
   > 
   > void periodic() {
   >     count++;
   >     sigreturn();
   > }
   > 
   > int test0() {
   >     int i;
   >     count = 0;
   >     for (i = 0; i < 1000 * 500000; i++)
   >         if (count > 0)
   >             return i;
   > }
   > ```
   >
   > 在test0的循环中等待2秒后执行periodic，periodic返回到test0继续执行，test0返回循环次数。test0是leaf，没保存ra，在返回时出错，此时的ra指向test0被中断的位置，应该发生死循环，但事实是在gdb下，某一次执行return i后按n，出错而进入到trap，sepc指向for循环内的某条指令，scause为page fault，stval为无法映射的虚拟地址
   >
   > ![](diary_img/21.png)
   >
   > 下图为缺页指令附近的代码，ret并没跳出函数，反而在下面的代码中反复执行，注意`addi sp,sp,16`，这条指令反复执行导致栈向高地址溢出，所以在`ld s0,8(sp)`时访问了未映射的地址
   >
   > ![](diary_img/22.png)


2024.4.12

在sigreturn中恢复原来的现场，在linux中，现场保存在用户栈，handler结束后ret到一段汇编指令调用rt_sigreturn系统调用(这段指令是由操作系统添加，可在linux源码arch/riscv/kernel/signal.c:setup_rt_frame中看到)，在系统调用中恢复原来的现场。

![](diary_img/23.png)

> <span name="signal">根据地址，这条指令在libc.so中，但栈帧的确是linux构建的，换musl-gcc编译也一样，所以内核是如何确定这条指令地址的？在musl源码中搜索\_\_restore\_rt，看到这段断码是在signal/x86_64/restore.s中，并在signal/sigaction.c:__libc_sigaction下看到对这个汇编函数的引用，说明在调用signal时libc要把\_\_restore\_rt传递给内核</span>

sigreturn是系统调用，a0的值会被修改，所以在syscall中判断系统调用是否为sigreturn，如果是，则不修改a0

现场保护在用户栈，handler执行时也要用栈，在sigreturn中如何找到现场地址？通过fp来找，不管函数是否是leaf，fp的位置固定

test2可重入没通过，还没看懂测试用例和可重入

2024.4.13

把proc->alarm_cnt放在sys_sigreturn中重置即可，这样在handler执行完之前alarm_cnt都不会二次归零

optional challenge在backtrace中打印代码行号和函数名找到一个[参考](https://zhuanlan.zhihu.com/p/401961538)，需要把kernel/kernel可执行文件加载到xv6文件系统，解析其中的.debug_line。后续有时间再实现(果然程序无法读取自身的section)

./grade-lab-traps测试bttest时会卡住，可能是在等待qemu的输出。用python调试器看看是停在哪

应该是代码框架的问题，在`./grade-lib-traps:addr2line:subprocess.Popen([f], stdout=devnull, stderr=devnull).communicate()`处阻塞。qemu应该输出到xv6.out文件中，调试时output到缓冲区，无法在文件中看到内容，调试结束后能看到。开始下一个lab，不在该处浪费时间



### cow

2024.1.16

<span name="linux_cow">[详解Linux内核COW机制](https://zhuanlan.zhihu.com/p/464765151)</span>

linux中通过缺页pte(和缺页地址？)确定fault page，再通过fault page找到vm_area_struct，该结构记录段的访问权限，页引用数记录在struct page中

xv6的内存分配算法决定其不支持vm_area_struct和struct page，引用计数能用一个大数组存储，但原来的访问权限放哪？看讲义说在cow lab要用到PTE的RSW位，看来只能在此设置PTE_COW位(或者说标记该页原来是否可写，只有原来可写的页才有cow)，写该页时发生复制

初步解决方案：

* 在fork中调用uvmcopy，该函数不分配新页，而是让父子进程共享页。对于PTE_W的页，在pte中添加PTE_COW，清空PTE_W(即改为只读)，在全局g_page_ref(该数组放哪合适？)中增加引用计数

  注意正常设置PTE_V，在lazy allocation中才清除该位，fork中所有页都是存在的

* 缺页中断时(只有写中断才处理，指令不存在/read不存在都直接kill)，过程如下：

  ```c
  align_pg(&addr);  // 异常地址要对齐到页
  pte = get_pte(addr);
  if (pte & PTE_COW) {  // 合法访问，处理cow
  	int ref_cnt = g_page_ref[addr];  // 是页的引用数，而不是pte的引用数
  	if (ref_cnt <= 0)
  		panic();
  	if (ref_cnt > 1) {  // 分配新页
  		phy_page = pg_alloc();
          if (phy_page == nullptr)  // 内存耗尽直接退出
              kill();
  		set_addr(phy_page, pte);  // 把pte关联到phy_page
          旧页copy到新页;
  		g_page_ref[addr]--;  // 共享页引用数减1
          g_page_ref[phy_page]++;
          if (g_page_ref[phy_page] != 1)  // 新页的引用数应该是1
              panic();
  	}
  	// 如果只有当前进程还在引用该页，修改pte标志即可
  	pte &= PTE_W;
  	清除PTE_COW;
  	// 重新执行指令的操作留给caller进行
  }
  else  // 非法访问
  	kill();
  ```

* 进程退出时，引用页减1，减到0才释放

* kmalloc中对分配页的引用计数+1

* 对于copyout()，直接在函数中检测pte是否直接可写，不需要触发page fault，调用上述伪函数添加页
* “proc”会fork init，在fork前如何记录页引用数？应该是在vm.c:kvminit*函数中修改



2024.4.17

基本框架写完，运行后在uvmunmap报错()，应该是init进程引用计数问题

改为在kalloc里申请页是增加计数，kfree在回收前计数\-\-，若不为0，则panic

运行在exec:proc_freepagetable:uvmfree:freewalk:kfree报错，因为释放页表前计数没-1。注意页表不共享

在sysfile.c:sys_exec中kfree时又报错，调用kfree的地方太多，改为在kfree中计数\-\-，减完后判0

还报错，kfree时检查到计数器位255，是在kinit:freerange中以kfree的形式来构建链表，但此时计数器全0；只能在freerange中先对计数器++了

vm.c:copy_on_write有问题导致命令执行无反馈

![](diary_img/24.png)

发现是trapframe->epc设置错误，该值保存了fault指令地址，不需要修改(原来想成syscall，-8才是重新执行)。修正后指令ls能执行，但执行完每按一次回车，sh会缺页4次，前2次和后2次一样。应该是sys_wait的问题

![](diary_img/25.png)

wait中调用了copyout，但还没修改该函数。不过已经能过cowtest.c第一个测试点

copyout修改较简单，对cow页调用copy_on_write即可

测试usertests.c报错

![](diary_img/26.png)

mem：分配全部->释放全部->分配全部

kalloc中在最后返回nullptr，但引用计数没判断，该为如果nullptr就提前返回

usertests.c尝试访问最大内存以上的部分，在copy_on_write中处理正确，但walk()会直接panic，修改为返回0即可

回过来看，该实验也实现了lazy allocation，只差exec执行后不立即从磁盘加载程序就和现代操作系统完成的大部分功能相同了(可能相关功能只差引入盘交换区？)



### thread

待补充进程切换过程



### lock

#### memory allocator

或许能在xv6上实现musl的内存分配器？

先参考slab实现简单的算法：定义struct kmem_cache管理所有cpu(struct array_cache)，kmem_cache中有共享的free_pg。当array_cache的free_pg用完后，先从kmem_cache申请获取batch_cnt个free_pg；若kmem_cache的free_pg已用完，则其他所有cpu向该cpu转移transfer个free_pg，注意该过程后kmem_cache.shared_num == 0.

用大锁并关中断，在一个cpu下正常，在三个cpu下有页丢失

不确定修改了某个地方后三个cpu正常运行

在kalloctest的test2中，每次countfree完输出一个‘.’，否则循环50次太慢不确定是否还正常

#### buffer cache

在slot双向链表中指针出错发生死循环(bio.c:find_in_slot)。刚开始在binit中找到bug，当slot->next指向自己时，slot->next->prev不是slot->prev，因为struct slot和struct buf不同，而slot->next是struct buf*。现在的bug或许跟这个类似

在struct slot中添加struct buf成员(不得不增加struct slot的大小了)，简化insert、delete、find后，系统能启动，执行ls报错

![](diary_img/27.png)

修改struct buf->data为指针，数组放在struct bcache中，buf->data指向一个一维数组作为缓冲区。

修改MAXOPBLOCKS为2能执行ls，为3报错。

将hash()固定返回0，MAXOPBLOCKS为2或3报`bget: no buffers`

prinrf会影响buf使用导致bug不一定能复现。在MAXOPBLOCKS=3，hash()->0，用于debug的printf打开的情况下，执行ls会报`panic: ilock: no type`。查看kernel/fs.c:ilock，读取dinode的buf未命中，不应受到缓存管理的影响，目标dinode(inum=23, blockno=12)位于buf->data + sizeof(dinode)\*7，但index >= 7后数据为全0，index==6数据异常，index < 6数据正常。怀疑是写入出错

blockno=12在之前有多次读出均没出错，怎么这里出错？？而且没发生写入，只是blockno所在的缓存用于其他block，没有任何一次命中



### fs

#### bigfile

在make qemu中测试bigfile正常，./grade-lab-fs running bigfile报错

![](diary_img/28.png)

测试bigfile过于费时，需要修改./grade-lab-fs中`test running bigfile`和`test usertests`的超时参数。修改后bigfile可通过，usertests报错如下

![](diary_img/29.png)



2024.5.5

测试时已经添加了部分symlink代码，commit当前changes为A，checkout到原始提交为B，用`git checkout <commit-A-hash> -- kernel/fs.c kernel/fs.h`复制A的fs.\*文件到B，直接测试exectest仍报上述错误。查看实验文档说明还要修改kernel/file.h中的struct inode—>addr，修改后测试通过。用`git diff <commit-A-hash>..HEAD -- kernel/file.h`查看果然A中没修改，所以bug出在这里



#### symlink

2024.5.5

除了添加建立symlink的代码，还要修改读取symlink相关代码

写完sys_symlink(添加符号链接)测试发生死锁。还有sys_open没改写

通过create获取的inode已上锁，上述bug由二次上锁导致

2024.5.7

在kernel/sysfile.c:open_symlink中，如果符号链接发生循环，在对inode上锁时会死锁(在上一次open该路径上过一次)。解决方法：在循环前先iunlockput传入的inode，这样下一次能正常访问该inode



### mmap

