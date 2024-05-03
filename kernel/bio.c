// Buffer cache.
//
// The buffer cache is a linked list of buf structures holding
// cached copies of disk block contents.  Caching disk blocks
// in memory reduces the number of disk reads and also provides
// a synchronization point for disk blocks used by multiple processes.
//
// Interface:
// * To get a buffer for a particular disk block, call bread.
// * After changing buffer data, call bwrite to write it to disk.
// * When done with the buffer, call brelse.
// * Do not use the buffer after calling brelse.
// * Only one process at a time can use a buffer,
//     so do not keep them longer than necessary.

#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"
#include "buf.h"

#define NSLOT 13

struct slot {
    struct buf *next;
    struct buf *prev;
    struct spinlock lock;
};

struct {
    struct spinlock lock;
    struct buf buf[NBUF];

    struct slot hash_table[NSLOT];

} bcache;

void binit(void) {

    initlock(&bcache.lock, "bcache");

    for (int i = 0; i < NSLOT; i++) {
        struct slot *s = &bcache.hash_table[i];
        initlock(&s->lock, "bcache_slot");
        s->next = (struct buf*)s;
        s->prev = (struct buf*)s;
    }

    for (int i = 0; i < NBUF; i++) {
        struct buf *b = &bcache.buf[i];
        struct slot *s = &bcache.hash_table[i % NSLOT];

        // insert into the head
        b->next = s->next;
        b->prev = (struct buf*)s;
        s->next->prev = b;
        s->next = b;

        initsleeplock(&b->lock, "buffer");
    }
}

static int hash(uint dev, uint blockno) {
    return ((dev << 8) ^ blockno) % NSLOT;
}

static struct buf *find_in_slot(uint dev, uint blockno, struct slot *s) {
    struct buf *b;

    for (b = s->next; b != (struct buf*)s; b = b->next) {
        if (b->dev == dev && b->blockno == blockno) {
            return b;
        }
    }
    return 0;
}

static struct buf *find_in_whole(struct slot **selected_slot) {
    for (int i = 0; i < NSLOT; i++) {
        struct slot *s = &bcache.hash_table[i];
        acquire(&s->lock);
        for (struct buf *b = s->next; b != (struct buf*)s; b = b->next)
            if (b->refcnt == 0) {
                *selected_slot = s;
                return b;
            }
        release(&s->lock);
    }
    return 0;
}

static void delete_buf(struct buf *b) {
    b->next->prev = b->prev;
    b->prev->next = b->next;
}

static struct buf *bget(uint dev, uint blockno) {

    acquire(&bcache.lock);

    struct slot *selected_slot = &bcache.hash_table[hash(dev, blockno)];
    acquire(&selected_slot->lock);

    struct buf *b;

    b = find_in_slot(dev, blockno, selected_slot);
    if (b) {
        delete_buf(b);
        b->refcnt++;
    } else {
        b = find_in_whole(&selected_slot);
        if (b) {
            delete_buf(b);
            b->dev = dev;
            b->blockno = blockno;
            b->valid = 0;
            b->refcnt = 1;
        } else
            goto panicing;
    }

    release(&selected_slot->lock);
    acquiresleep(&b->lock);

    release(&bcache.lock);

    return b;

panicing:
    panic("bget: no buffers");
}

// Return a locked buf with the contents of the indicated block.
struct buf *bread(uint dev, uint blockno) {
    struct buf *b;

    b = bget(dev, blockno);
    if (!b->valid) {
        virtio_disk_rw(b, 0);
        b->valid = 1;
    }
    return b;
}

// Write b's contents to disk.  Must be locked.
void bwrite(struct buf *b) {
    if (!holdingsleep(&b->lock))
        panic("bwrite");
    virtio_disk_rw(b, 1);
}

static void insert(struct buf *b, struct slot *s) {
    b->next = s->next;
    b->prev = (struct buf*)s;
    s->next->prev = b;
    s->next = b;
}

void brelse(struct buf *b) {
    if (!holdingsleep(&b->lock))
        panic("brelse");

    acquire(&bcache.lock);

    releasesleep(&b->lock);
    b->refcnt--;
    struct slot *s = &bcache.hash_table[hash(b->dev, b->blockno)];
    acquire(&s->lock);
    insert(b, s);
    release(&s->lock);

    release(&bcache.lock);
}

void bpin(struct buf *b) {
    acquire(&bcache.lock);
    b->refcnt++;
    release(&bcache.lock);
}

void bunpin(struct buf *b) {
    acquire(&bcache.lock);
    b->refcnt--;
    release(&bcache.lock);
}
