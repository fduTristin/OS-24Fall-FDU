#include <kernel/proc.h>
#include <kernel/mem.h>
#include <kernel/sched.h>
#include <aarch64/mmu.h>
#include <common/list.h>
#include <common/string.h>
#include <common/pid.h>
#include <kernel/printk.h>
#include <kernel/paging.h>
#include <fs/inode.h>

Proc root_proc;
BITMAP pid_map;

void kernel_entry();
void proc_entry();

static SpinLock plock;

// init_kproc initializes the kernel process
// NOTE: should call after kinit
void init_kproc()
{
    // TODO:
    // 1. init global resources (e.g. locks, semaphores)
    // 2. init the root_proc (finished)
    init_spinlock(&plock);
    init_bitmap(&pid_map);

    init_proc(&root_proc);
    root_proc.parent = &root_proc;
    start_proc(&root_proc, kernel_entry, 123456);
}

void init_proc(Proc *p)
{
    // TODO:
    // setup the Proc with kstack and pid allocated
    // NOTE: be careful of concurrency

    acquire_spinlock(&plock);

    memset(p, 0, sizeof(Proc));
    p->pid = alloc_pid(&pid_map);
    init_sem(&p->childexit, 0);
    p->killed = FALSE;
    init_list_node(&p->children);
    ASSERT(_empty_list(&p->children));
    init_list_node(&p->ptnode);
    p->kstack = kalloc_page();
    memset((void *)p->kstack, 0, PAGE_SIZE);
    init_schinfo(&p->schinfo);
    init_pgdir(&p->pgdir);
    p->kcontext =
            (KernelContext *)((u64)p->kstack + PAGE_SIZE - 16 -
                              sizeof(KernelContext) - sizeof(UserContext));
    p->ucontext = (UserContext *)((u64)p->kstack + PAGE_SIZE - 16 -
                                  sizeof(UserContext));
    if (inodes.root)
        p->cwd = inodes.share(inodes.root);
    init_oftable(&p->oftable);
    release_spinlock(&plock);
}

Proc *create_proc()
{
    Proc *p = kalloc(sizeof(Proc));
    init_proc(p);
    return p;
}

void set_parent_to_this(Proc *proc)
{
    // TODO: set the parent of proc to thisproc
    // NOTE: maybe you need to lock the process tree
    // NOTE: it's ensured that the old proc->parent = NULL
    acquire_spinlock(&plock);
    proc->parent = thisproc();
    _insert_into_list(&thisproc()->children, &proc->ptnode);
    release_spinlock(&plock);
}

int start_proc(Proc *p, void (*entry)(u64), u64 arg)
{
    // TODO:
    // 1. set the parent to root_proc if NULL
    // 2. setup the kcontext to make the proc start with proc_entry(entry, arg)
    // 3. activate the proc and return its pid
    // NOTE: be careful of concurrency
    if (p->parent == NULL) {
        acquire_spinlock(&plock);
        p->parent = &root_proc;
        _insert_into_list(&root_proc.children, &p->ptnode);
        release_spinlock(&plock);
    }
    p->kcontext->lr = (u64)&proc_entry;
    p->kcontext->x0 = (u64)entry;
    p->kcontext->x1 = (u64)arg;
    int id = p->pid;
    activate_proc(p);
    return id;
}

int wait(int *exitcode)
{
    // TODO:
    // 1. return -1 if no children
    // 2. wait for childexit
    // 3. if any child exits, clean it up and return its pid and exitcode
    // NOTE: be careful of concurrency
    int id = 0;
    auto this = thisproc();

    if (_empty_list(&this->children)) {
        return -1;
    }

    bool res = wait_sem(&this->childexit);
    if (res) {
        acquire_spinlock(&plock);
        _for_in_list(p, &this->children)
        {
            if (p == &this->children)
                continue;
            Proc *child = container_of(p, Proc, ptnode);
            if (is_zombie(child)) {
                *exitcode = child->exitcode;
                id = child->pid;
                _detach_from_list(p);
                kfree_page(child->kstack);
                kfree(child);
                free_pid(&pid_map, id); // free pid here
                break;
            }
        }
        release_spinlock(&plock);
        return id;
    } else {
        PANIC();
    }
}

NO_RETURN void exit(int code)
{
    // TODO:
    // 1. set the exitcode
    // 2. clean up the resources
    // 3. transfer children to the root_proc, and notify the root_proc if there is zombie
    // 4. sched(ZOMBIE)
    // NOTE: be careful of concurrency

    acquire_spinlock(&plock);

    auto this = thisproc();
    this->exitcode = code;

    while (!_empty_list(&this->children)) {
        ListNode *p = this->children.next;
        auto proc = container_of(p, Proc, ptnode);
        _detach_from_list(p);
        proc->parent = &root_proc;
        bool notify = is_zombie(proc);
        _insert_into_list(root_proc.children.prev, p);
        if (notify)
            post_sem(&root_proc.childexit);
    }

    post_sem(&this->parent->childexit);

    acquire_sched_lock();
    release_spinlock(&plock);
    free_pgdir(&this->pgdir);
    // Final
    decrement_rc(&this->cwd->rc);
    for (int i = 0; i < NOFILE; i++) {
        if (this->oftable.file[i]) {
            file_close(this->oftable.file[i]);
            this->oftable.file[i] = 0;
        }
    }
    sched(ZOMBIE);
    PANIC(); // prevent the warning of 'no_return function returns'
}

Proc *dfs(Proc *p, int pid)
{
    if (p->pid == pid) {
        return p;
    } else if (!_empty_list(&p->children)) {
        _for_in_list(child, &p->children)
        {
            if (child == &p->children) {
                continue;
            }
            Proc *childproc = container_of(child, Proc, ptnode);
            Proc *ret = dfs(childproc, pid);
            if (ret) {
                return ret;
            }
        }
    }
    return NULL;
}

int kill(int pid)
{
    // TODO:
    // Set the killed flag of the proc to true and return 0.
    // Return -1 if the pid is invalid (proc not found).
    acquire_spinlock(&plock);
    Proc *p = dfs(&root_proc, pid);
    if (p && !is_unused(p)) {
        p->killed = TRUE;
        alert_proc(p);
        release_spinlock(&plock);
        return 0;
    }
    release_spinlock(&plock);
    return -1;
}

/*
 * Create a new process copying p as the parent.
 * Sets up stack to return as if from system call.
 */
void trap_return();
int fork()
{
    /**
     * (Final) TODO BEGIN
     * 
     * 1. Create a new child process.
     * 2. Copy the parent's memory space.
     * 3. Copy the parent's trapframe.
     * 4. Set the parent of the new proc to the parent of the parent.
     * 5. Set the state of the new proc to RUNNABLE.
     * 6. Activate the new proc and return its pid.
     */
    // 1. 4.
    printk("my pid: %d\n", thisproc()->pid);
    Proc *parent = thisproc(), *child = create_proc();
    acquire_spinlock(&plock);
    child->parent = parent;
    _insert_into_list(&parent->children, &child->ptnode);
    release_spinlock(&plock);

    // 2.
    memcpy((void *)child->ucontext, (void *)parent->ucontext,
           sizeof(UserContext));
    child->ucontext->gregs[0] = 0;

    // sections
    acquire_spinlock(&parent->pgdir.lock);
    ListNode *head = &parent->pgdir.section_head;
    _for_in_list(p, head)
    {
        if (p == head)
            continue;
        struct section *sec = container_of(p, struct section, stnode);
        struct section *new_sec =
                (struct section *)kalloc(sizeof(struct section));
        init_section(new_sec);
        new_sec->begin = sec->begin;
        new_sec->end = sec->end;
        new_sec->flags = sec->flags;
        if (sec->fp) {
            new_sec->fp = file_dup(sec->fp);
            new_sec->offset = sec->offset;
            new_sec->length = sec->length;
        }
        _insert_into_list(&child->pgdir.section_head, &new_sec->stnode);
        for (auto va = PAGE_BASE(sec->begin); va < sec->end; va += PAGE_SIZE) {
            auto pte = get_pte(&parent->pgdir, va, false);
            if (pte && (*pte & PTE_VALID)) {
                *pte |= PTE_RO; // freeze shared page
                vmmap(&child->pgdir, va, (void *)P2K(PTE_ADDRESS(*pte)),
                      PTE_FLAGS(*pte));
                kshare_page(P2K(PTE_ADDRESS(*pte)));
            }
        }
    }
    release_spinlock(&parent->pgdir.lock);

    memset((void *)&child->oftable, 0, sizeof(struct oftable));
    if (child->cwd != parent->cwd) {
        OpContext ctx;
        bcache.begin_op(&ctx);
        inodes.put(&ctx, child->cwd);
        bcache.end_op(&ctx);
        child->cwd = inodes.share(parent->cwd);
    }

    for (int i = 0; i < NOFILE; i++) {
        if (parent->oftable.file[i]) {
            child->oftable.file[i] = file_dup(parent->oftable.file[i]);
        } else
            break;
    }
    start_proc(child, trap_return, 0);
    return child->pid;
    /* (Final) TODO END */
}