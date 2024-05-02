// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

void freerange(void *pa_start, void *pa_end);

extern char end[]; // first address after kernel.
                   // defined by kernel.ld.

struct run {
    struct run *next;
};

struct array_cache {
    struct spinlock lock;
    int free_num;
    struct run *freelist;
};

struct {
    struct array_cache cpu_cache[NCPU];
    struct run *shared;
    int shared_num;
    int batch_cnt; // transfer batch_cnt pages from shared to cpu_cache if
                   // cpu_cache is empty
    int limit;
    int transfer;
    struct spinlock lock;
} kmem_cache;

static struct run *get_pg(struct array_cache *ac) {
    if (ac->free_num <= 0)
        return 0;

    struct run *r;
    r = ac->freelist;
    ac->freelist = r->next;

    return r;
}

static void transfer(struct run **from, struct run **to, int num) {
    struct run *tmp;
    while (num > 0) {
        tmp = *from;
        if (*from == 0)
            panic("transfer: from is NULL");
        *from = (*from)->next;
        tmp->next = *to;
        *to = tmp;
        num--;
    }
}

static void fill(struct array_cache *ac) {
    // fill from the shared
    if (kmem_cache.shared) {
        // transfer_num = min(batch_cnt, shared_num)
        int transfer_num = kmem_cache.batch_cnt < kmem_cache.shared_num
                               ? kmem_cache.batch_cnt
                               : kmem_cache.shared_num;
        transfer(&kmem_cache.shared, &ac->freelist, transfer_num);
        kmem_cache.shared_num -= transfer_num;
        ac->free_num += transfer_num;
    }
    // fill from other cpu_cache.
    // all cpu will transfer to this one
    else {
        for (int i = 0; i < NCPU; i++) {
            struct array_cache *other = &kmem_cache.cpu_cache[i];
            if (other == ac)
                continue;
            int transfer_num = kmem_cache.transfer < ac->free_num
                                   ? kmem_cache.transfer
                                   : ac->free_num;
            transfer(&kmem_cache.shared, &ac->freelist, transfer_num);
            other->free_num -= transfer_num;
            ac->free_num += transfer_num;
        }
    }
}

static void *cpu_alloc() {
    int cpu_id = cpuid();
    struct array_cache *ac = &kmem_cache.cpu_cache[cpu_id];
    struct run *free_pg = get_pg(ac);
    if (free_pg == 0) {
        fill(ac);
        free_pg = get_pg(ac);
    }

    if (free_pg)
        ac->free_num--;

    pop_off();

    return (void *)free_pg;
}

static void cpu_free(struct run *free_pg) {
    if (free_pg == 0)
        return;

    int cpu_id = cpuid();
    struct array_cache *ac = &kmem_cache.cpu_cache[cpu_id];

    if (ac->free_num >= kmem_cache.limit) {
        free_pg->next = kmem_cache.shared;
        kmem_cache.shared = free_pg;
        kmem_cache.shared_num++;
    } else {
        free_pg->next = ac->freelist;
        ac->freelist = free_pg;
        ac->free_num++;
    }
}

void kinit() {
    const int BUF_SIZE = 10;
    char buf[BUF_SIZE];
    memset(buf, 0, BUF_SIZE);
    strncpy(buf, "kmem", BUF_SIZE);
    initlock(&kmem_cache.lock, buf);
    for (int i = 0; i < NCPU; i++) {
        snprintf(buf, BUF_SIZE, "%d", i);
        initlock(&kmem_cache.cpu_cache[i].lock, buf);
    }

    kmem_cache.shared = 0;
    kmem_cache.shared_num = 0;
    kmem_cache.batch_cnt = 16;
    kmem_cache.limit = kmem_cache.batch_cnt; // limit >= batch_cnt
    kmem_cache.transfer = kmem_cache.limit / NCPU / 2;

    freerange(end, (void *)PHYSTOP);
}

void freerange(void *pa_start, void *pa_end) {
    char *p;
    p = (char *)PGROUNDUP((uint64)pa_start);
    for (; p + PGSIZE <= (char *)pa_end; p += PGSIZE)
        kfree(p);
}

// Free the page of physical memory pointed at by pa,
// which normally should have been returned by a
// call to kalloc().  (The exception is when
// initializing the allocator; see kinit above.)
void kfree(void *pa) {

    if (((uint64)pa % PGSIZE) != 0 || (char *)pa < end || (uint64)pa >= PHYSTOP)
        panic("kfree");

    // Fill with junk to catch dangling refs.
    memset(pa, 1, PGSIZE);

    cpu_free((struct run *)pa);
}

// Allocate one 4096-byte page of physical memory.
// Returns a pointer that the kernel can use.
// Returns 0 if the memory cannot be allocated.
void *kalloc(void) {
    push_off();
    acquire(&kmem_cache.lock);

    void *free_pg = cpu_alloc();
    if (free_pg)
        memset((char *)free_pg, 5, PGSIZE); // fill with junk

    release(&kmem_cache.lock);
    pop_off();

    return free_pg;
}
