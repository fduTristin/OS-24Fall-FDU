#include <common/string.h>
#include <fs/inode.h>
#include <kernel/mem.h>
#include <kernel/printk.h>

/**
    @brief the private reference to the super block.

    @note we need these two variables because we allow the caller to
            specify the block cache and super block to use.
            Correspondingly, you should NEVER use global instance of
            them.

    @see init_inodes
 */
static const SuperBlock *sblock;

/**
    @brief the reference to the underlying block cache.
 */
static const BlockCache *cache;

/**
    @brief global lock for inode layer.

    Use it to protect anything you need.

    e.g. the list of allocated blocks, ref counts, etc.
 */
static SpinLock lock;

/**
    @brief the list of all allocated in-memory inodes.

    We use a linked list to manage all allocated inodes.

    You can implement your own data structure if you want better performance.

    @see Inode
 */
static ListNode head;

Inode *find(usize inode_no);

// return which block `inode_no` lives on.
static INLINE usize to_block_no(usize inode_no)
{
    return sblock->inode_start + (inode_no / (INODE_PER_BLOCK));
}

// return the pointer to on-disk inode.
static INLINE InodeEntry *get_entry(Block *block, usize inode_no)
{
    return ((InodeEntry *)block->data) + (inode_no % INODE_PER_BLOCK);
}

// return address array in indirect block.
static INLINE u32 *get_addrs(Block *block)
{
    return ((IndirectBlock *)block->data)->addrs;
}

// initialize inode tree.
void init_inodes(const SuperBlock *_sblock, const BlockCache *_cache)
{
    init_spinlock(&lock);
    init_list_node(&head);
    sblock = _sblock;
    cache = _cache;

    if (ROOT_INODE_NO < sblock->num_inodes)
        inodes.root = inodes.get(ROOT_INODE_NO);
    else
        printk("(warn) init_inodes: no root inode.\n");
}

// initialize in-memory inode.
static void init_inode(Inode *inode)
{
    init_sleeplock(&inode->lock);
    init_rc(&inode->rc);
    init_list_node(&inode->node);
    inode->inode_no = 0;
    inode->valid = false;
}

// see `inode.h`.
static usize inode_alloc(OpContext *ctx, InodeType type)
{
    ASSERT(type != INODE_INVALID);

    // TODO
    usize inode_no = 1;
    while (inode_no < sblock->num_inodes) {
        usize block_no = to_block_no(inode_no);
        Block *b = cache->acquire(block_no);
        InodeEntry *IE = get_entry(b, inode_no);
        if (IE->type == INODE_INVALID) {
            memset(IE, 0, sizeof(InodeEntry));
            IE->type = type;
            cache->sync(ctx, b);
            cache->release(b);
            return inode_no;
        }
        cache->release(b);
        inode_no++;
    }
    PANIC();
    return 0;
}

// see `inode.h`.
static void inode_lock(Inode *inode)
{
    ASSERT(inode->rc.count > 0);
    // TODO
    unalertable_wait_sem(&inode->lock);
}

// see `inode.h`.
static void inode_unlock(Inode *inode)
{
    ASSERT(inode->rc.count > 0);
    // TODO
    post_sem(&inode->lock);
}

// see `inode.h`.
static void inode_sync(OpContext *ctx, Inode *inode, bool do_write)
{
    // TODO
    usize block_no = to_block_no(inode->inode_no);
    Block *b = cache->acquire(block_no);
    if (do_write) {
        if (!inode->valid) {
            cache->release(b);
            PANIC();
        }
        InodeEntry *IE = get_entry(b, inode->inode_no);
        memcpy(IE, &inode->entry, sizeof(InodeEntry));
        cache->sync(ctx, b);
    }
    if (!do_write) {
        if (!inode->valid) {
            InodeEntry *IE = get_entry(b, inode->inode_no);
            memcpy(&inode->entry, IE, sizeof(InodeEntry));
            inode->valid = TRUE;
        }
    }
    cache->release(b);
}

// see `inode.h`.
static Inode *inode_get(usize inode_no)
{
    ASSERT(inode_no > 0);
    ASSERT(inode_no < sblock->num_inodes);
    acquire_spinlock(&lock);
    // TODO
    Inode *ret = find(inode_no);
    if (ret) {
        increment_rc(&ret->rc);
        release_spinlock(&lock);
        return ret;
    }
    Inode *new_inode = (Inode *)kalloc(sizeof(Inode));
    init_inode(new_inode);
    new_inode->inode_no = inode_no;
    increment_rc(&new_inode->rc);

    inode_lock(new_inode);
    inode_sync(NULL, new_inode, FALSE);
    inode_unlock(new_inode);

    new_inode->valid = TRUE;
    _insert_into_list(&head, &new_inode->node);
    release_spinlock(&lock);
    return new_inode;
}

Inode *find(usize inode_no)
{
    _for_in_list(p, &head)
    {
        if (p == &head) {
            continue;
        }
        Inode *i = container_of(p, Inode, node);
        if (i->inode_no == inode_no) {
            return i;
        }
    }
    return NULL;
}

// see `inode.h`.
static void inode_clear(OpContext *ctx, Inode *inode)
{
    // TODO
    InodeEntry *IE = &inode->entry;
    for (usize i = 0; i != INODE_NUM_DIRECT; i++) {
        usize block_no = inode->entry.addrs[i];
        if (block_no) {
            cache->free(ctx, block_no);
        }
    }

    if (IE->indirect) {
        Block *ib = cache->acquire(IE->indirect);
        u32 *indir_addrs = get_addrs(ib);
        for (usize i = 0; i < INODE_NUM_INDIRECT; i++) {
            u32 block_no = indir_addrs[i];
            if (block_no) {
                cache->free(ctx, block_no);
            }
        }
        cache->release(ib);
        cache->free(ctx, IE->indirect);
    }

    inode->entry.indirect = NULL;
    memset((void *)inode->entry.addrs, 0, sizeof(u32) * INODE_NUM_DIRECT);
    inode->entry.num_bytes = 0;
    inode_sync(ctx, inode, TRUE);
}

// see `inode.h`.
static Inode *inode_share(Inode *inode)
{
    // TODO
    increment_rc(&inode->rc);
    return inode;
}

// see `inode.h`.
static void inode_put(OpContext *ctx, Inode *inode)
{
    // TODO
    acquire_spinlock(&lock);
    if (inode->rc.count == 1 && inode->entry.num_links == 0) {
        inode_lock(inode);
        inode_clear(ctx, inode);
        inode->entry.type = INODE_INVALID;
        inode_sync(ctx, inode, TRUE);
        inode_unlock(inode);
        _detach_from_list(&inode->node);
        kfree(inode);
    } else {
        decrement_rc(&inode->rc);
    }
    release_spinlock(&lock);
}

/**
    @brief get which block is the offset of the inode in.

    e.g. `inode_map(ctx, my_inode, 1234, &modified)` will return the block_no
    of the block that contains the 1234th byte of the file
    represented by `my_inode`.

    If a block has not been allocated for that byte, `inode_map` will
    allocate a new block and update `my_inode`, at which time, `modified`
    will be set to true.

    HOWEVER, if `ctx == NULL`, `inode_map` will NOT try to allocate any new block,
    and when it finds that the block has not been allocated, it will return 0.
    
    @param[out] modified true if some new block is allocated and `inode`
    has been changed.

    @return usize the block number of that block, or 0 if `ctx == NULL` and
    the required block has not been allocated.

    @note the caller must hold the lock of `inode`.
 */
static usize inode_map(OpContext *ctx, Inode *inode, usize offset,
                       bool *modified)
{
    // TODO
    usize idx = offset / BLOCK_SIZE;
    InodeEntry *entry = &inode->entry;
    u32 block_no = 0;
    if (idx < INODE_NUM_DIRECT) {
        block_no = entry->addrs[idx];
        if (!block_no && ctx) {
            block_no = cache->alloc(ctx);
            entry->addrs[idx] = block_no;
            *modified = TRUE;
        }
    } else {
        if (!entry->indirect) {
            if (!ctx) {
                return 0;
            }
            entry->indirect = cache->alloc(ctx);
            // *modified = TRUE; can be omitted, since block_no must be 0
        }
        idx = idx - INODE_NUM_DIRECT;
        ASSERT(idx < INODE_NUM_INDIRECT);
        Block *ib = cache->acquire(entry->indirect);
        block_no = get_addrs(ib)[idx];
        if (!block_no && ctx) {
            block_no = cache->alloc(ctx);
            *modified = TRUE;
            get_addrs(ib)[idx] = block_no;
        }
        cache->release(ib);
    }
    return block_no;
}

// see `inode.h`.
static usize inode_read(Inode *inode, u8 *dest, usize offset, usize count)
{
    InodeEntry *entry = &inode->entry;
    if (count + offset > entry->num_bytes)
        count = entry->num_bytes - offset;
    usize end = offset + count;
    ASSERT(offset <= entry->num_bytes);
    ASSERT(end <= entry->num_bytes);
    ASSERT(offset <= end);

    // TODO
    usize read_pointer = offset;
    while (read_pointer < end) {
        usize block_no = inode_map(NULL, inode, read_pointer, NULL);
        if (!block_no) {
            PANIC();
        }
        usize begin = read_pointer % BLOCK_SIZE;
        Block *block_read = cache->acquire(block_no);
        u8 *data = block_read->data;
        usize len = MIN(BLOCK_SIZE - begin, end - read_pointer);
        memcpy(dest + read_pointer - offset, data + begin, len);
        cache->release(block_read);
        read_pointer += len;
    }
    ASSERT(read_pointer - offset == count);
    return read_pointer - offset;
}

// see `inode.h`.
static usize inode_write(OpContext *ctx, Inode *inode, u8 *src, usize offset,
                         usize count)
{
    InodeEntry *entry = &inode->entry;
    usize end = offset + count;
    ASSERT(offset <= entry->num_bytes);
    ASSERT(end <= INODE_MAX_BYTES);
    ASSERT(offset <= end);

    // TODO
    usize write_pointer = offset;
    while (write_pointer < end) {
        bool modified;
        usize block_no = inode_map(ctx, inode, write_pointer, &modified);
        if (!block_no) {
            PANIC();
        }
        usize begin = write_pointer % BLOCK_SIZE;
        Block *block_write = cache->acquire(block_no);
        u8 *data = block_write->data;
        usize len = MIN(BLOCK_SIZE - begin, end - write_pointer);
        memcpy(data + begin, src + write_pointer - offset, len);
        cache->sync(ctx, block_write);
        cache->release(block_write);
        write_pointer += len;
    }
    if (end > entry->num_bytes) {
        inode->entry.num_bytes = end;
        inode_sync(ctx, inode, TRUE);
    }
    ASSERT(write_pointer - offset == count);
    return write_pointer - offset;
}

// see `inode.h`.
static usize inode_lookup(Inode *inode, const char *name, usize *index)
{
    InodeEntry *entry = &inode->entry;
    ASSERT(entry->type == INODE_DIRECTORY);

    // TODO
    DirEntry DE;
    usize idx = 0;
    usize offset = 0;
    while (offset < entry->num_bytes) {
        inode_read(inode, (u8 *)&DE, offset, sizeof(DirEntry));
        if (DE.inode_no && !strncmp(DE.name, name, FILE_NAME_MAX_LENGTH)) {
            if (index) {
                *index = idx;
            }
            return DE.inode_no;
        }
        idx += 1;
        offset += sizeof(DirEntry);
    }
    return 0;
}

// see `inode.h`.
static usize inode_insert(OpContext *ctx, Inode *inode, const char *name,
                          usize inode_no)
{
    InodeEntry *entry = &inode->entry;
    ASSERT(entry->type == INODE_DIRECTORY);

    // TODO
    DirEntry DE;
    usize offset = 0;
    usize index = 0;
    usize ret = __UINT64_MAX__;
    while (offset < entry->num_bytes) {
        inode_read(inode, (u8 *)&DE, offset, sizeof(DirEntry));
        if (DE.inode_no && !strncmp(DE.name, name, FILE_NAME_MAX_LENGTH)) {
            return -1;
        }
        if (!DE.inode_no) {
            DE.inode_no = inode_no;
            strncpy(DE.name, name, FILE_NAME_MAX_LENGTH);
            inode_write(ctx, inode, (u8 *)&DE, offset, sizeof(DirEntry));
            ret = index;
        }
        offset += sizeof(DirEntry);
        index += 1;
    }
    if (ret == __UINT64_MAX__) {
        DE.inode_no = inode_no;
        strncpy(DE.name, name, FILE_NAME_MAX_LENGTH);
        inode_write(ctx, inode, (u8 *)&DE, offset, sizeof(DirEntry));
        ret = index;
    }
    return ret;
}

// see `inode.h`.
static void inode_remove(OpContext *ctx, Inode *inode, usize index)
{
    // TODO
    usize offset = index * sizeof(DirEntry);
    DirEntry DE;
    memset(&DE, 0, sizeof(DirEntry));
    inode_write(ctx, inode, (u8 *)&DE, offset, sizeof(DirEntry));
    if (offset + sizeof(DirEntry) == inode->entry.num_bytes) {
        inode->entry.num_bytes -= sizeof(DirEntry);
    }
}

InodeTree inodes = {
    .alloc = inode_alloc,
    .lock = inode_lock,
    .unlock = inode_unlock,
    .sync = inode_sync,
    .get = inode_get,
    .clear = inode_clear,
    .share = inode_share,
    .put = inode_put,
    .read = inode_read,
    .write = inode_write,
    .lookup = inode_lookup,
    .insert = inode_insert,
    .remove = inode_remove,
};