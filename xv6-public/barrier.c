/*
  Barrier synchronization (kernel)

  description:
  - A barrier is initialized once with a target count N using barrier_init(N).
  - Processes that need to synchronize call barrier_check().
  - The first N-1 processes that call barrier_check() will block.
  - The N-th process to call barrier_check() will wake all waiting processes
    AND wait itself until all N processes have arrived.
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
  
  if(barrier.initialized)
    return;  // Already initialized; prevent double init
  
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
    return;

  acquire(&barrier.lock);
  barrier.arrived++;
  
  // If this is the N-th arrival, wake all waiting processes
  if(barrier.arrived == barrier.target){
    wakeup(&barrier);
  }
  
  // ALL processes (including N-th) must wait until all N have arrived
  // They will be woken by the N-th arrival (or by spurious wakeup check)
  while(barrier.arrived < barrier.target){
    sleep(&barrier, &barrier.lock);
  }
  
  // At this point, all N processes have arrived and been woken.
  // Release the lock and proceed.
  release(&barrier.lock);
}
