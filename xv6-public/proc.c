#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "x86.h"
#include "proc.h"
#include "spinlock.h"
#include <stddef.h>

struct {
  struct spinlock lock;
  struct proc proc[NPROC];
} ptable;

static struct proc *initproc;

int nextpid = 1;
extern void forkret(void);
extern void trapret(void);

static void wakeup1(void *chan);

void
pinit(void)
{
  initlock(&ptable.lock, "ptable");
}

// Must be called with interrupts disabled
int
cpuid() {
  return mycpu()-cpus;
}

// Must be called with interrupts disabled to avoid the caller being
// rescheduled between reading lapicid and running through the loop.
struct cpu*
mycpu(void)
{
  int apicid, i;
  
  if(readeflags()&FL_IF)
    panic("mycpu called with interrupts enabled\n");
  
  apicid = lapicid();
  // APIC IDs are not guaranteed to be contiguous. Maybe we should have
  // a reverse map, or reserve a register to store &cpus[i].
  for (i = 0; i < ncpu; ++i) {
    if (cpus[i].apicid == apicid)
      return &cpus[i];
  }
  panic("unknown apicid\n");
}

// Disable interrupts so that we are not rescheduled
// while reading proc from the cpu structure
struct proc*
myproc(void) {
  struct cpu *c;
  struct proc *p;
  pushcli();
  c = mycpu();
  p = c->proc;
  popcli();
  return p;
}

//PAGEBREAK: 32
// Look in the process table for an UNUSED proc.
// If found, change state to EMBRYO and initialize
// state required to run in the kernel.
// Otherwise return 0.
/*
 // ensure thread-related defaults for a freshly allocated proc
  p->mainthread = p;    // by default a process is its own main thread
  p->isthread = 0;
  p->ustack = 0;
  */
static struct proc*
allocproc(void)
{
  struct proc *p;
  char *sp;

  acquire(&ptable.lock);

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == UNUSED)
      goto found;

  release(&ptable.lock);
  return 0;

found:
  p->state = EMBRYO;
  p->pid = nextpid++;

  release(&ptable.lock);

  // Allocate kernel stack.
  if((p->kstack = kalloc()) == 0){
    p->state = UNUSED;
    return 0;
  }
  sp = p->kstack + KSTACKSIZE;

  // Leave room for trap frame.
  sp -= sizeof *p->tf;
  p->tf = (struct trapframe*)sp;

  // Set up new context to start executing at forkret,
  // which returns to trapret.
  sp -= 4;
  *(uint*)sp = (uint)trapret;

  sp -= sizeof *p->context;
  p->context = (struct context*)sp;
  memset(p->context, 0, sizeof *p->context);
  p->context->eip = (uint)forkret;

  // ensure thread-related defaults for a freshly allocated proc
  p->mainthread = p;    // by default a process is its own main thread
  p->isthread = 0;
  p->ustack = 0;

  return p;
}

//PAGEBREAK: 32
// Set up first user process.
void
userinit(void)
{
  struct proc *p;
  extern char _binary_initcode_start[], _binary_initcode_size[];

  p = allocproc();
  
  initproc = p;
  if((p->pgdir = setupkvm()) == 0)
    panic("userinit: out of memory?");
  inituvm(p->pgdir, _binary_initcode_start, (int)_binary_initcode_size);
  p->sz = PGSIZE;
  memset(p->tf, 0, sizeof(*p->tf));
  p->tf->cs = (SEG_UCODE << 3) | DPL_USER;
  p->tf->ds = (SEG_UDATA << 3) | DPL_USER;
  p->tf->es = p->tf->ds;
  p->tf->ss = p->tf->ds;
  p->tf->eflags = FL_IF;
  p->tf->esp = PGSIZE;
  p->tf->eip = 0;  // beginning of initcode.S

  safestrcpy(p->name, "initcode", sizeof(p->name));
  p->cwd = namei("/");

  // this assignment to p->state lets other cores
  // run this process. the acquire forces the above
  // writes to be visible, and the lock is also needed
  // because the assignment might not be atomic.
  acquire(&ptable.lock);

  p->state = RUNNABLE;

  release(&ptable.lock);
}

// Grow current process's memory by n bytes.
// Return 0 on success, -1 on failure.
int
growproc(int n)
{
  uint sz;
  struct proc *curproc = myproc();

  sz = curproc->sz;
  if(n > 0){
    if((sz = allocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  } else if(n < 0){
    if((sz = deallocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  }
  curproc->sz = sz;
  switchuvm(curproc);
  return 0;
}

// Create a new process copying p as the parent.
// Sets up stack to return as if from system call.
// Caller must set state of returned proc to RUNNABLE.
int
fork(void)
{
  int i, pid;
  struct proc *np;
  struct proc *curproc = myproc();



  // Allocate process.
  if((np = allocproc()) == 0){
    return -1;
  }

  // Copy process state from proc.
  if((np->pgdir = copyuvm(curproc->pgdir, curproc->sz)) == 0){
    kfree(np->kstack);
    np->kstack = 0;
    np->state = UNUSED;
    return -1;
  }
  np->sz = curproc->sz;
  np->parent = curproc;
  *np->tf = *curproc->tf;

  // Clear %eax so that fork returns 0 in the child.
  np->tf->eax = 0;

  for(i = 0; i < NOFILE; i++)
    if(curproc->ofile[i])
      np->ofile[i] = filedup(curproc->ofile[i]);
  np->cwd = idup(curproc->cwd);

  safestrcpy(np->name, curproc->name, sizeof(curproc->name));

  pid = np->pid;

  acquire(&ptable.lock);

  np->state = RUNNABLE;

  release(&ptable.lock);

  np->mainthread = np;  // mark itself as main thread
  np->isthread = 0;     // it's a process, not a thread

  return pid;
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait() to find out it exited.
void exit(void);

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int wait(void);

//PAGEBREAK: 42
// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run
//  - swtch to start running that process
//  - eventually that process transfers control
//      via swtch back to the scheduler.
void
scheduler(void)
{
  struct proc *p;
  struct cpu *c = mycpu();
  c->proc = 0;
  
  for(;;){
    // Enable interrupts on this processor.
    sti();

    // Loop over process table looking for process to run.
    acquire(&ptable.lock);
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->state != RUNNABLE)
        continue;

      // Switch to chosen process.  It is the process's job
      // to release ptable.lock and then reacquire it
      // before jumping back to us.
      c->proc = p;
      switchuvm(p);
      p->state = RUNNING;

      swtch(&(c->scheduler), p->context);
      switchkvm();

      // Process is done running for now.
      // It should have changed its p->state before coming back.
      c->proc = 0;
    }
    release(&ptable.lock);

  }
}

// Enter scheduler.  Must hold only ptable.lock
// and have changed proc->state. Saves and restores
// intena because intena is a property of this
// kernel thread, not this CPU. It should
// be proc->intena and proc->ncli, but that would
// break in the few places where a lock is held but
// there's no process.
void
sched(void)
{
  int intena;
  struct proc *p = myproc();

  if(!holding(&ptable.lock))
    panic("sched ptable.lock");
  if(mycpu()->ncli != 1)
    panic("sched locks");
  if(p->state == RUNNING)
    panic("sched running");
  if(readeflags()&FL_IF)
    panic("sched interruptible");
  intena = mycpu()->intena;
  swtch(&p->context, mycpu()->scheduler);
  mycpu()->intena = intena;
}

// Give up the CPU for one scheduling round.
void
yield(void)
{
  acquire(&ptable.lock);  //DOC: yieldlock
  myproc()->state = RUNNABLE;
  sched();
  release(&ptable.lock);
}

// A fork child's very first scheduling by scheduler()
// will swtch here.  "Return" to user space.
void
forkret(void)
{
  static int first = 1;
  // Still holding ptable.lock from scheduler.
  release(&ptable.lock);

  if (first) {
    // Some initialization functions must be run in the context
    // of a regular process (e.g., they call sleep), and thus cannot
    // be run from main().
    first = 0;
    iinit(ROOTDEV);
    initlog(ROOTDEV);
  }

  // Return to "caller", actually trapret (see allocproc).
}

// Atomically release lock and sleep on chan.
// Reacquires lock when awakened.
void
sleep(void *chan, struct spinlock *lk)
{
  struct proc *p = myproc();
  
  if(p == 0)
    panic("sleep");

  if(lk == 0)
    panic("sleep without lk");

  // Must acquire ptable.lock in order to
  // change p->state and then call sched.
  // Once we hold ptable.lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup runs with ptable.lock locked),
  // so it's okay to release lk.
  if(lk != &ptable.lock){  //DOC: sleeplock0
    acquire(&ptable.lock);  //DOC: sleeplock1
    release(lk);
  }
  // Go to sleep.
  p->chan = chan;
  p->state = SLEEPING;

  sched();

  // Tidy up.
  p->chan = 0;

  // Reacquire original lock.
  if(lk != &ptable.lock){  //DOC: sleeplock2
    release(&ptable.lock);
    acquire(lk);
  }
}

//PAGEBREAK!
// Wake up all processes sleeping on chan.
// The ptable lock must be held.
static void
wakeup1(void *chan)
{
  struct proc *p;

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == SLEEPING && p->chan == chan)
      p->state = RUNNABLE;
}

// Wake up all processes sleeping on chan.
void
wakeup(void *chan)
{
  acquire(&ptable.lock);
  wakeup1(chan);
  release(&ptable.lock);
}

// Kill the process with the given pid.
// Process won't exit until it returns
// to user space (see trap in trap.c).
int
kill(int pid)
{
  struct proc *p;

  acquire(&ptable.lock);
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->pid == pid){
      p->killed = 1;
      // Wake process from sleep if necessary.
      if(p->state == SLEEPING)
        p->state = RUNNABLE;
      release(&ptable.lock);
      return 0;
    }
  }
  release(&ptable.lock);
  return -1;
}

//PAGEBREAK: 36
// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.
void
procdump(void)
{
  static char *states[] = {
  [UNUSED]    "unused",
  [EMBRYO]    "embryo",
  [SLEEPING]  "sleep ",
  [RUNNABLE]  "runble",
  [RUNNING]   "run   ",
  [ZOMBIE]    "zombie"
  };
  int i;
  struct proc *p;
  char *state;
  uint pc[10];

  cprintf("\nPID  STATE   NAME         TYPE    MAINTHREAD  USTACK\n");
  cprintf("--------------------------------------------------------\n");
  
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->state == UNUSED)
      continue;
    if(p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";
    
    // Show process/thread info
    if(p->isthread) {
      cprintf("%d   %s   %s   THREAD   %d          %p", 
              p->pid, state, p->name, 
              p->mainthread ? p->mainthread->pid : 0,
              p->ustack);
    } else {
      cprintf("%d   %s   %s   PROCESS  %d          %p", 
              p->pid, state, p->name,
              p->mainthread ? p->mainthread->pid : 0,
              p->ustack);
    }
    
    if(p->state == SLEEPING){
      getcallerpcs((uint*)p->context->ebp+2, pc);
      for(i=0; i<10 && pc[i] != 0; i++)
        cprintf(" %p", pc[i]);
    }
    cprintf("\n");
  }
  cprintf("\n");
}

//dfifference between wait() and waitpid(): wait() waits for all the child process to be executed for the current process, while waitpid() is used to wait for a particular process + wait for child process- asking for their status

//implementation of waitpid
int
waitpid(int pid)
{
  struct proc *p;
  int havekids;
  struct proc *curproc = myproc();

  acquire(&ptable.lock);
  for(;;){
    //check if the given pid is a child process of the current process and look for exited child.
    havekids = 0;
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->parent != curproc || p->pid != pid)
        continue;
      //found the required child process
      havekids = 1;
      if(p->state == ZOMBIE){
        int reaped = p->pid;
        kfree(p->kstack);
        p->kstack = 0;
        freevm(p->pgdir);
        p->pid = 0;
        p->parent = 0;
        p->name[0] = 0;
        p->killed = 0;
        p->state = UNUSED;
        release(&ptable.lock);
        return reaped;
      }
    }

    //if there was no such child process or current process was killed return -1
    if(!havekids || curproc->killed){
      release(&ptable.lock);
      return -1;
    }

    // Wait for child to exit.
    sleep(curproc, &ptable.lock);  //DOC: wait-sleep
  }
}

/*
 * thread_create: Create a new thread sharing the address space.
 * - Allocates a proc slot and user stack page.
 * - Sets up the stack with the argument and entry point.
 * - Returns the thread ID (tid).
 */
int thread_create(uint *tid, void *(*func)(void *), void *arg)
{
  struct proc *np;
  struct proc *curproc = myproc();

  // Allocate new proc structure
  if((np = allocproc()) == 0)
    return -1;

  // Share the same address space
  np->pgdir = curproc->pgdir;
  np->sz = curproc->sz;

  np->parent = curproc;
  np->mainthread = curproc->mainthread ? curproc->mainthread : curproc;
  np->isthread = 1;

  // Allocate user stack for the new thread
  uint stack_addr = PGROUNDUP(curproc->sz);
  uint new_sz = stack_addr + PGSIZE;
  if(allocuvm(np->pgdir, curproc->sz, new_sz) == 0){
    kfree(np->kstack);
    np->state = UNUSED;
    return -1;
  }
  np->ustack = stack_addr;
  
  // Update the process size to include the new stack
  curproc->sz = new_sz;
  np->sz = new_sz;

  // Set up stack pointer at bottom of stack page (stack grows upward)
  uint sp = stack_addr + PGSIZE;
  
  // Push argument first (will be at esp+4 when function starts)
  sp -= 4;
  if(copyout(np->pgdir, sp, &arg, sizeof(void*)) < 0){
    deallocuvm(np->pgdir, new_sz, curproc->sz);
    kfree(np->kstack);
    np->state = UNUSED;
    return -1;
  }
  
  // Push fake return address (will be at esp when function starts)
  sp -= 4;
  uint fake_ret = 0;
  if(copyout(np->pgdir, sp, &fake_ret, sizeof(uint)) < 0){
    deallocuvm(np->pgdir, new_sz, curproc->sz);
    kfree(np->kstack);
    np->state = UNUSED;
    return -1;
  }

  // Copy trapframe and modify it for new thread
  *(np->tf) = *(curproc->tf);
  np->tf->eip = (uint)func; // start at function
  np->tf->esp = sp;         // stack pointer: points to return addr, arg is at sp+4

  // Store thread ID in user memory
  if(copyout(np->pgdir, (uint)tid, &(np->pid), sizeof(uint)) < 0){
    deallocuvm(np->pgdir, new_sz, curproc->sz);
    kfree(np->kstack);
    np->state = UNUSED;
    return -1;
  }

  safestrcpy(np->name, curproc->name, sizeof(curproc->name));
  
  // Debug output
  cprintf("thread_create: Created thread %d (mainthread=%d, func=%p, arg=%p, stack=%p)\n",
          np->pid, np->mainthread->pid, func, arg, stack_addr);
  
  acquire(&ptable.lock);
  np->state = RUNNABLE;
  release(&ptable.lock);

  return np->pid;
}


/*
 * thread_exit: Exit the current thread (non-main only).
 * - Mark as ZOMBIE and wake joiners.
 * - Do not free memory here (handled in join/wait).
 */
void thread_exit(void)
{
  struct proc *curproc = myproc();
  
  // If mainthread calls this, it's a no-op
  if(curproc->mainthread == curproc) {
    cprintf("thread_exit: Mainthread %d called thread_exit (no-op)\n", curproc->pid);
    return;
  }

  cprintf("thread_exit: Thread %d exiting (mainthread=%d)\n", 
          curproc->pid, curproc->mainthread->pid);

  acquire(&ptable.lock);

  curproc->state = ZOMBIE;

  // Wake up the mainthread that might be waiting in thread_join
  wakeup1(curproc->mainthread);
  
  sched();
  // Should not reach here
  release(&ptable.lock);
}


/*
 * thread_join: Wait for a thread to exit and clean up.
 * - Wait for the thread to become ZOMBIE.
 * - Free thread-specific resources (kernel stack, user stack).
 * - Return tid on success, -1 on error.
 */
int thread_join(uint tid)
{
  struct proc *p;
  struct proc *curproc = myproc();

  // Mainthreads cannot join themselves
  if(curproc->pid == tid) {
    cprintf("thread_join: Mainthread %d cannot join itself\n", curproc->pid);
    return -1;
  }

  cprintf("thread_join: Mainthread %d waiting for thread %d\n", curproc->pid, tid);

  acquire(&ptable.lock);
  for(;;){
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->pid != tid || p->mainthread != curproc->mainthread)
        continue;
      // Only mainthreads can join their spawned threads
      if(p->mainthread != curproc)
        continue;
      if(p->state == ZOMBIE){
        cprintf("thread_join: Thread %d reaped by mainthread %d\n", tid, curproc->pid);
        
        // Free thread-specific user stack page (do not free the shared pgdir)
        if(p->ustack){
          // deallocuvm signature: deallocuvm(pgdir, oldsz, newsz)
          // We want to deallocate from (ustack + PGSIZE) down to ustack
          deallocuvm(p->pgdir, p->ustack + PGSIZE, p->ustack);
          p->ustack = 0;
        }
        if(p->kstack){
          kfree(p->kstack);
          p->kstack = 0;
        }
        // Reset proc slot (keep pgdir intact because it's shared)
        p->pid = 0;
        p->parent = 0;
        p->name[0] = 0;
        p->killed = 0;
        p->state = UNUSED;
        p->isthread = 0;
        p->mainthread = 0;
        release(&ptable.lock);
        return tid;
      }
    }
    sleep(curproc, &ptable.lock); // sleep until thread exits
  }
}


/* --- small changes to existing exit() and wait() behavior --- */

/* in exit(), at top: if current is a thread, hand off to thread_exit_impl() 

void exit(void)
{
  struct proc *curproc = myproc();
  if(curproc->isthread)
  {
    thread_exit();
    return;
  }
  ...
}


*/

void
exit(void)
{
  struct proc *curproc = myproc();

  if(curproc->isthread){
    // non-main thread: perform thread exit
    thread_exit();
    return;
  }

  // Close all open files.
  for(int fd = 0; fd < NOFILE; fd++){
    if(curproc->ofile[fd]){
      fileclose(curproc->ofile[fd]);
      curproc->ofile[fd] = 0;
    }
  }

  begin_op();
  iput(curproc->cwd);
  end_op();
  curproc->cwd = 0;

  acquire(&ptable.lock);

  // Parent might be sleeping in wait().
  wakeup1(curproc->parent);

  // Pass abandoned children to init.
  for(struct proc *p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->parent == curproc){
      p->parent = initproc;
      if(p->state == ZOMBIE)
        wakeup1(initproc);
    }
  }

  // Jump into the scheduler, never to return.
  curproc->state = ZOMBIE;
  sched();
  panic("zombie exit");
}

// In wait(), ensure we only reap mainthreads and when reaping a mainthread, reap its threads.
// Replace child selection/cleanup code with logic that reaps mainthread and its threads.
int
wait(void)
{
  struct proc *p;
  struct proc *curproc = myproc();
  int havekids;
  int pid;

  acquire(&ptable.lock);
  for(;;){
    havekids = 0;
    // look for a child that is a mainthread (isthread==0)
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      if(p->parent != curproc)
        continue;
      if(p->isthread)  // skip threads: wait() waits for main thread only
        continue;
      havekids = 1;
      if(p->state == ZOMBIE){
        // reap all threads belonging to this mainthread first
        struct proc *t;
        for(t = ptable.proc; t < &ptable.proc[NPROC]; t++){
          if(t->isthread && t->mainthread == p){
            // free t-specific resources
            if(t->kstack){
              kfree(t->kstack);
              t->kstack = 0;
            }
            if(t->ustack){
              // deallocuvm signature: deallocuvm(pgdir, oldsz, newsz)
              deallocuvm(t->pgdir, t->ustack + PGSIZE, t->ustack);
              t->ustack = 0;
            }
            // mark UNUSED
            t->pid = 0;
            t->parent = 0;
            t->name[0] = 0;
            t->killed = 0;
            t->state = UNUSED;
            t->isthread = 0;
            t->mainthread = 0;
          }
        }

        // now reap the mainthread p (free kernel stack + full address space)
        pid = p->pid;
        if(p->kstack){
          kfree(p->kstack);
          p->kstack = 0;
        }
        freevm(p->pgdir);
        p->pid = 0;
        p->parent = 0;
        p->name[0] = 0;
        p->killed = 0;
        p->state = UNUSED;
        release(&ptable.lock);
        return pid;
      }
    }

    if(!havekids || curproc->killed){
      release(&ptable.lock);
      return -1;
    }

    sleep(curproc, &ptable.lock);
  }
}