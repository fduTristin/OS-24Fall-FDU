#include <common/string.h>
#include <fs/inode.h>
#include <kernel/mem.h>
#include <kernel/printk.h>
#include <kernel/sched.h>
#include <kernel/console.h>

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

    if (ROOT_INODE_NO < sblock->num_inodes) {
        inodes.root = inodes.get(ROOT_INODE_NO);
        // printk("type:%d\n",inodes.root->entry.type);
    } else
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
        // printk("inode %lld free!\n", inode->inode_no);
        inode_sync(ctx, inode, TRUE);
        inode_unlock(inode);
        _detach_from_list(&inode->node);
        kfree(inode);
    } else {
        decrement_rc(&inode->rc);
    }
    release_spinlock(&lock);
}

static void inode_unlockput(OpContext *ctx, Inode *inode)
{
    inode_unlock(inode);
    inode_put(ctx, inode);
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
    if (inode->entry.type == INODE_DEVICE) {
        return console_read(inode, (char *)dest, count);
    }
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
    // printk("pages: %lld\n", left_page_cnt());
    if (inode->entry.type == INODE_DEVICE) {
        return console_write(inode, (char *)src, count);
    }

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
    // printk("pages: %lld\n", left_page_cnt());
    return write_pointer - offset;
}

// see `inode.h`.
static usize inode_lookup(Inode *inode, const char *name, usize *index)
{
    InodeEntry *entry = &inode->entry;
    // ASSERT(entry->type == INODE_DIRECTORY);

    // TODO
    DirEntry de;
    usize idx = 0;
    usize offset = 0;
    while (offset < entry->num_bytes) {
        inode_read(inode, (u8 *)&de, offset, sizeof(DirEntry));
        if (de.inode_no && !strncmp(de.name, name, FILE_NAME_MAX_LENGTH)) {
            if (index) {
                *index = idx;
            }
            return de.inode_no;
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
    DirEntry de;
    usize offset = 0;
    usize index = 0;
    usize ret = __UINT64_MAX__;
    if (inode_lookup(inode, name, NULL)) {
        return -1;
    }
    while (offset < entry->num_bytes) {
        inode_read(inode, (u8 *)&de, offset, sizeof(DirEntry));
        if (!de.inode_no) {
            de.inode_no = inode_no;
            strncpy(de.name, name, FILE_NAME_MAX_LENGTH);
            inode_write(ctx, inode, (u8 *)&de, offset, sizeof(DirEntry));
            ret = index;
            break;
        }
        offset += sizeof(DirEntry);
        index += 1;
    }
    if (ret == __UINT64_MAX__) {
        de.inode_no = inode_no;
        strncpy(de.name, name, FILE_NAME_MAX_LENGTH);
        inode_write(ctx, inode, (u8 *)&de, offset, sizeof(DirEntry));
        ret = index;
    }
    return ret;
}

// see `inode.h`.
static void inode_remove(OpContext *ctx, Inode *inode, usize index)
{
    // TODO
    usize offset = index * sizeof(DirEntry);
    DirEntry de;
    memset(&de, 0, sizeof(DirEntry));
    inode_write(ctx, inode, (u8 *)&de, offset, sizeof(DirEntry));
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
    .unlockput = inode_unlockput,
    .read = inode_read,
    .write = inode_write,
    .lookup = inode_lookup,
    .insert = inode_insert,
    .remove = inode_remove,
};

/**
    @brief read the next path element from `path` into `name`.
    
    @param[out] name next path element.

    @return const char* a pointer offseted in `path`, without leading `/`. If no
    name to remove, return NULL.

    @example 
    skipelem("a/bb/c", name) = "bb/c", setting name = "a",
    skipelem("///a//bb", name) = "bb", setting name = "a",
    skipelem("a", name) = "", setting name = "a",
    skipelem("", name) = skipelem("////", name) = NULL, not setting name.
 */
static const char *skipelem(const char *path, char *name)
{
    const char *s;
    int len;

    while (*path == '/')
        path++;
    if (*path == 0)
        return 0;
    s = path;
    while (*path != '/' && *path != 0)
        path++;
    len = path - s;
    if (len >= FILE_NAME_MAX_LENGTH)
        memmove(name, s, FILE_NAME_MAX_LENGTH);
    else {
        memmove(name, s, len);
        name[len] = 0;
    }
    while (*path == '/')
        path++;
    return path;
}

// /**
//     @brief look up and return the inode for `path`.

//     If `nameiparent`, return the inode for the parent and copy the final
//     path element into `name`.

//     @param path a relative or absolute path. If `path` is relative, it is
//     relative to the current working directory of the process.

//     @param[out] name the final path element if `nameiparent` is true.

//     @return Inode* the inode for `path` (or its parent if `nameiparent` is true),
//     or NULL if such inode does not exist.

//     @example
//     namex("/a/b", false, name) = inode of b,
//     namex("/a/b", true, name) = inode of a, setting name = "b",
//     namex("/", true, name) = NULL (because "/" has no parent!)
//  */
static Inode *namex(const char *path, bool nameiparent, char *name,
                    OpContext *ctx)
{
    /* (Final) TODO BEGIN */
    Inode *ip, *next;
    if (*path == '/') {
        ip = inodes.get(ROOT_INODE_NO);
    } else {
        ip = inodes.share(thisproc()->cwd);
    }
    while ((path = skipelem(path, name)) != 0) {
        inodes.lock(ip);
        if (ip->entry.type != INODE_DIRECTORY) {
            inodes.unlockput(ctx, ip);
            printk("FROM %s, %d, NOT A DIR!\n", __FILE__, __LINE__);
            return NULL;
        }
        if (nameiparent && *path == '\0') {
            // Stop one level early
            inodes.unlock(ip);
            return ip;
        }
        usize inode_no = inodes.lookup(ip, name, 0);
        if (inode_no == 0) {
            inodes.unlockput(ctx, ip);
            // printk("FROM %s, %d, name %s not found!\n", __FILE__, __LINE__,
            //        name);
            return NULL;
        }
        next = inodes.get(inode_no);
        inodes.unlockput(ctx, ip);
        ip = next;
    }
    if (nameiparent) {
        inodes.put(ctx, ip);
        return NULL;
    }
    /* (Final) TODO END */
    return ip;
}

Inode *namei(const char *path, OpContext *ctx)
{
    char name[FILE_NAME_MAX_LENGTH];
    return namex(path, false, name, ctx);
}

Inode *nameiparent(const char *path, char *name, OpContext *ctx)
{
    return namex(path, true, name, ctx);
}

/**
    @brief get the stat information of `ip` into `st`.
    
    @note the caller must hold the lock of `ip`.
 */
void stati(Inode *ip, struct stat *st)
{
    st->st_dev = 1;
    st->st_ino = ip->inode_no;
    st->st_nlink = ip->entry.num_links;
    st->st_size = ip->entry.num_bytes;
    switch (ip->entry.type) {
    case INODE_REGULAR:
        st->st_mode = S_IFREG;
        break;
    case INODE_DIRECTORY:
        st->st_mode = S_IFDIR;
        break;
    case INODE_DEVICE:
        st->st_mode = 0;
        break;
    default:
        PANIC();
    }
}