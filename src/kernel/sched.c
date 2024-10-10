#include <kernel/sched.h>
#include <kernel/proc.h>
#include <kernel/mem.h>
#include <kernel/printk.h>
#include <aarch64/intrinsic.h>
#include <kernel/cpu.h>
#include <common/rbtree.h>

extern bool panic_flag;

extern void swtch(KernelContext *new_ctx, KernelContext **old_ctx);

static SpinLock rqlock;
static ListNode rq;

void init_sched()
{
    // TODO: initialize the scheduler
    // 1. initialize the resources (e.g. locks, semaphores)
    // 2. initialize the scheduler info of each CPU
    init_spinlock(&rqlock);
    init_list_node(&rq);

    for(int i = 0; i != NCPU; i++)
    {
        Proc* p = kalloc(sizeof(Proc));
        p->idle = true;
        p->state = RUNNING;
        cpus[i].sched.thisproc = cpus[i].sched.idle = p;
    }
}

Proc *thisproc()
{
    // TODO: return the current process
    return cpus[cpuid()].sched.thisproc;
}

void init_schinfo(struct schinfo *p)
{
    // TODO: initialize your customized schinfo for every newly-created process
    init_list_node(&p->rq);
}

void acquire_sched_lock()
{
    // TODO: acquire the sched_lock if need
    acquire_spinlock(&rqlock);
}

void release_sched_lock()
{
    // TODO: release the sched_lock if need
    release_spinlock(&rqlock);
}

bool is_zombie(Proc *p)
{
    bool r;
    acquire_sched_lock();
    r = p->state == ZOMBIE;
    release_sched_lock();
    return r;
}

bool activate_proc(Proc *p)
{
    // TODO:
    // if the proc->state is RUNNING/RUNNABLE, do nothing
    // if the proc->state if SLEEPING/UNUSED, set the process state to RUNNABLE and add it to the sched queue
    // else: panic
    acquire_sched_lock();
    if(p->state == RUNNING || p->state == RUNNABLE)
    {
        release_sched_lock();
        return false;
    }
    else if(p->state == SLEEPING || p->state == UNUSED)
    {
        p->state = RUNNABLE;
        _insert_into_list(&rq, &(p->schinfo.rq));
    }
    else{
        PANIC();
    }
    release_sched_lock();
    return true;
}

static void update_this_state(enum procstate new_state)
{
    // TODO: if you use template sched function, you should implement this routinue
    // update the state of current process to new_state, and not [remove it from the sched queue if
    // new_state=SLEEPING/ZOMBIE]
    if(new_state == RUNNABLE && !thisproc()->idle)
    {
        _insert_into_list(&rq, &thisproc()->schinfo.rq);
    }
    if((new_state == SLEEPING || new_state == ZOMBIE) && (thisproc()->state == RUNNABLE))
    {
        _detach_from_list(&thisproc()->schinfo.rq);
    }
    thisproc()->state = new_state;
}

static Proc *pick_next()
{
    // TODO: if using template sched function, you should implement this routinue
    // choose the next process to run, and return idle if no runnable process
    if(panic_flag)
        return cpus[cpuid()].sched.idle;
    _for_in_list(p, &rq)
    {
        if(p == &rq)
            continue;
        auto proc = container_of(p, Proc, schinfo.rq);
        if(proc->state == RUNNABLE)
            return proc;
    }
    return cpus[cpuid()].sched.idle;
}

static void update_this_proc(Proc *p)
{
    // TODO: you should implement this routinue
    // update thisproc to the choosen process
    // reset_clock(1000);
    cpus[cpuid()].sched.thisproc = p;
    if(!p->idle)
    {
        _detach_from_list(&p->schinfo.rq);
    }
}

// A simple scheduler.
// You are allowed to replace it with whatever you like.
// call with sched_lock
void sched(enum procstate new_state)
{
    auto this = thisproc();
    ASSERT(this->state == RUNNING);
    update_this_state(new_state);
    auto next = pick_next();
    update_this_proc(next);
    ASSERT(next->state == RUNNABLE);
    next->state = RUNNING;
    if (next != this) {
        swtch(next->kcontext, &this->kcontext);
    }
    release_sched_lock();
}

u64 proc_entry(void (*entry)(u64), u64 arg)
{
    release_sched_lock();
    set_return_addr(entry);
    return arg;
}
