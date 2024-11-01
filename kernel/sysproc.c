#include "types.h"
#include "riscv.h"
#include "param.h"
#include "defs.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"

uint64 sys_exit(void) {
    int n;
    argint(0, &n);
    exit(n);
    return 0; // not reached
}

uint64 sys_getpid(void) { return myproc()->pid; }

uint64 sys_fork(void) { return fork(); }

uint64 sys_wait(void) {
    uint64 p;
    argaddr(0, &p);
    return wait(p);
}

uint64 sys_sbrk(void) {
    uint64 addr;
    int n;

    argint(0, &n);
    addr = myproc()->sz;
    if (growproc(n) < 0)
        return -1;
    return addr;
}

uint64 sys_sleep(void) {
    int n;
    uint ticks0;

    argint(0, &n);
    acquire(&tickslock);
    ticks0 = ticks;
    while (ticks - ticks0 < n) {
        if (killed(myproc())) {
            release(&tickslock);
            return -1;
        }
        sleep(&ticks, &tickslock);
    }
    release(&tickslock);
    return 0;
}

#ifdef LAB_PGTBL
int sys_pgaccess(void) {
    uint64 base;
    uint pg_num;
    uint access_bits = 0;

    argaddr(0, &base);
    argint(1, (int *)&pg_num);
    if (base >= MAXVA || pg_num > 32)
        return -1;
    base = PGROUNDDOWN(base);

    for (int i = 0; i < pg_num; i++) {
        uint64 start_addr = base + i * PGSIZE;
        pte_t *pte = walk(myproc()->pagetable, start_addr, 0);
        if (pte == 0 || (*pte & PTE_V) == 0)
            goto bad;
        if (*pte & PTE_A) {
            access_bits |= 1 << i;
            *pte &= ~PTE_A;
        }
    }

    // return result to user space
    void *u_bits;
    argaddr(2, (uint64 *)&u_bits);
    if (copyout(myproc()->pagetable, (uint64)u_bits, (char *)&access_bits,
                sizeof(access_bits)) < 0)
        goto bad;

    return 0;
bad:
    return -1;
}

int sys_print_pgtbl(void) {
    vmprint(myproc()->pagetable);
    return 0;
}

#endif

uint64 sys_kill(void) {
    int pid;

    argint(0, &pid);
    return kill(pid);
}

// return how many clock tick interrupts have occurred
// since start.
uint64 sys_uptime(void) {
    uint xticks;

    acquire(&tickslock);
    xticks = ticks;
    release(&tickslock);
    return xticks;
}
