#include <kernel/proc.h>
#include <kernel/mem.h>
#include <kernel/sched.h>
#include <aarch64/mmu.h>
#include <common/list.h>
#include <common/string.h>
#include <common/pid.h>
#include <kernel/printk.h>

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