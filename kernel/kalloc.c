// Physical memory allocator, for user processes,
// kernel stacks, page-table pages,
// and pipe buffers. Allocates whole 4096-byte pages.

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "defs.h"

// #define DEBUG
#ifdef DEBUG
#define debug(...) printf(__VA_ARGS__)
#else
#define debug(...)
#endif

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
    acquire(&ac->lock);

    struct run *r = 0;
    if (ac->free_num > 0) {
        r = ac->freelist;
        ac->freelist = r->next;
        ac->free_num--;
    }

    release(&ac->lock);
    return r;
}

static void transfer(struct run **from, struct run **to, int num) {
    struct run *tmp;
    while (num > 0) {
        if (*from == 0)
            panic("transfer: from is NULL");
        tmp = *from;
        *from = (*from)->next;
        tmp->next = *to;
        *to = tmp;
        num--;
    }
}

// cpu_id: for acquiring lock in order
static void fill(struct array_cache *ac, int cpu_id) {
    acquire(&kmem_cache.lock);
    // fill from the shared
    if (kmem_cache.shared_num > 0) {
        // transfer_num = min(batch_cnt, shared_num)
        int transfer_num = kmem_cache.batch_cnt < kmem_cache.shared_num
                               ? kmem_cache.batch_cnt
                               : kmem_cache.shared_num;

        acquire(&ac->lock);

        transfer(&kmem_cache.shared, &ac->freelist, transfer_num);
        kmem_cache.shared_num -= transfer_num;
        ac->free_num += transfer_num;

        release(&ac->lock);
        release(&kmem_cache.lock);
    }
    // fill from other cpu_cache.
    // all cpu will transfer to this one
    else {
        release(&kmem_cache.lock);

        for (int i = 0; i < NCPU; i++) {
            if (i == cpu_id)
                continue;
            struct array_cache *other = &kmem_cache.cpu_cache[i];

            // acquire lock in order
            if (i < cpu_id) {
                acquire(&other->lock);
                acquire(&ac->lock);
            } else {
                acquire(&ac->lock);
                acquire(&other->lock);
            }

            // transfer_num = min(transfer, other->free_num)
            int transfer_num = kmem_cache.transfer < other->free_num
                                   ? kmem_cache.transfer
                                   : other->free_num;
            transfer(&other->freelist, &ac->freelist, transfer_num);
            other->free_num -= transfer_num;
            ac->free_num += transfer_num;

            // maybe the order of releasing lock takes no effect ?
            release(&other->lock);
            release(&ac->lock);
        }
    }
}

static void *cpu_alloc() {
    push_off();
    int cpu_id = cpuid();
    struct array_cache *ac = &kmem_cache.cpu_cache[cpu_id];
    pop_off();

    struct run *free_pg = get_pg(ac);
    if (free_pg == 0) {
        fill(ac, cpu_id);
        free_pg = get_pg(ac);
    }

    return (void *)free_pg;
}

static void cpu_free(struct run *free_pg) {
    if (free_pg == 0)
        return;

    push_off();
    int cpu_id = cpuid();
    struct array_cache *ac = &kmem_cache.cpu_cache[cpu_id];
    pop_off();

    if (ac->free_num >= kmem_cache.limit) {
        acquire(&kmem_cache.lock);

        free_pg->next = kmem_cache.shared;
        kmem_cache.shared = free_pg;
        kmem_cache.shared_num++;

        release(&kmem_cache.lock);
    } else {
        acquire(&ac->lock);

        free_pg->next = ac->freelist;
        ac->freelist = free_pg;
        ac->free_num++;

        release(&ac->lock);
    }
}

void kinit() {
    const int BUF_SIZE = 10;
    char buf[BUF_SIZE];
    memset(buf, 0, BUF_SIZE);
    strncpy(buf, "kmem", BUF_SIZE);
    initlock(&kmem_cache.lock, buf);

    struct array_cache *ac;
    for (int i = 0; i < NCPU; i++) {
        ac = &kmem_cache.cpu_cache[i];
        ac->free_num = 0;
        ac->freelist = 0;

        snprintf(buf, BUF_SIZE, "%d", i);
        initlock(&ac->lock, buf);
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

    void *free_pg = cpu_alloc();
    if (free_pg)
        memset((char *)free_pg, 5, PGSIZE); // fill with junk

    return free_pg;
}
