/*
  Barrier synchronization (kernel)

  description:
  - A barrier is initialized once with a target count N using barrier_init(N).
  - Processes that need to synchronize call barrier_check().
  - The first N-1 processes that call barrier_check() will block.
  - The N-th process to call barrier_check() will wake all waiting processes.
  - After the N-th process arrives, all N processes continue past the barrier.
  - This implementation is single-use (no reinitialization) as per assignment.
  - Implemented using a kernel spinlock and the kernel sleep/wakeup primitives.
*/

#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "x86.h"
#include "proc.h"
#include "spinlock.h"

static struct {
  struct spinlock lock;
  int initialized;   // 0 = not initialized, 1 = initialized
  int target;        // N
  int arrived;       // number arrived so far
} barrier;

void
barrier_init(int n)
{
  if(n <= 0)
    return;
  initlock(&barrier.lock, "barrier");
  acquire(&barrier.lock);
  barrier.target = n;
  barrier.arrived = 0;
  barrier.initialized = 1;
  release(&barrier.lock);
}

void
barrier_check(void)
{
  if(!barrier.initialized)
    return; // nothing to do if uninitialized

  acquire(&barrier.lock);
  barrier.arrived++;
  if(barrier.arrived >= barrier.target){
    // N-th arrival: wake all waiters (and allow them to proceed)
    wakeup(&barrier);
    release(&barrier.lock);
    return;
  }
  // wait until enough arrivals
  while(barrier.arrived < barrier.target){
    sleep(&barrier, &barrier.lock);
  }
  release(&barrier.lock);
}
