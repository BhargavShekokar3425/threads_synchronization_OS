# threads_synchronization_OS

This README provides a concise implementation guide for the xv6 project tasks (Parts A–D). Use this as a checklist while coding and testing.

Goals (high level)
- Part A: Implement waitpid syscall — allow a process to wait for a specific child PID.
- Part B: Implement a kernel barrier API — barrier_init and barrier_check in kernel.
- Part C: Provide user-level threads — thread_create, thread_exit, thread_join implemented in user space with minimal kernel support if required.
- Part D: Implement user-space spinlocks.

For each part the README lists:
- Expected API / syscall signatures
- Files to modify (suggested)
- Important semantics and edge cases
- Tests to add and how to build/run them

Part A — waitpid
- Behavior:
  - waitpid(pid, &status, options) should block until child with PID 'pid' exits or return -1 if no such child.
  - Should behave similarly to wait but target a specific child.
  - Return value: PID of terminated child, -1 on error.
- Kernel-level changes (suggested files):
  - syscall.h, syscall.c: add syscall number and syscall table entry.
  - sysproc.c: add sys_waitpid implementation that extracts arguments and calls internal kernel routine.
  - proc.c / proc.h: add kernel helper (e.g., waitpid) or extend existing wait to accept pid.
  - user.h / ulib.c / ulib.c (user library): provide user-space stub for waitpid.
  - defs.h: add prototype as needed.
- Edge cases:
  - Negative/zero PID handling (only valid PIDs).
  - Child not found: return -1.
  - Race conditions between exit and wait — ensure correct synchronization with process state and parent pointer.
- Test:
  - t_waitpid: create multiple children, wait for a specific one, validate return value and status.

Part B — Kernel barrier
- Behavior:
  - barrier_init(struct barrier *b, int count): initialize barrier for 'count' threads/processes.
  - barrier_check(struct barrier *b): block until 'count' callers have reached barrier, then release all.
- Kernel-level changes (suggested files):
  - Add barrier.c/h (kernel) implementing a struct barrier with sleep/wakeup or condition variables.
  - proc.c: may include barrier usage if testing processes.
  - Makefile: add barrier.o to kernel build list.
- Semantics:
  - Barrier can be reused if intended — clarify in comments. A simple single-use barrier is acceptable unless tests require reuse.
  - Use proper locking to protect the barrier's counter and waiting queue.
- Test:
  - t_barrier: spawn N threads/processes that call barrier_check; verify they all proceed only after N reach it.

Part C — User-level threads
- Behavior:
  - thread_create(thread_t *tid, void (*start_routine)(void*), void *arg)
  - thread_exit(void *retval)
  - thread_join(thread_t tid, void **retval)
- Implementation options:
  - Option 1 (preferred): Implement user-level threads entirely in user space using clone-like semantics implemented on top of existing process primitives, or if xv6 supports clone, use it.
  - Option 2: Minimal kernel support to create threads that share address space (requires kernel modifications in proc.c and VM code) — more invasive.
- Files to modify (suggested):
  - ulib.c / user-level library: implement thread API and thread stack allocation (malloc or mmap-like approach).
  - user.h: add prototypes and typedefs (e.g., typedef int thread_t).
  - If kernel changes required: proc.c, proc.h, syscall.h, syscall.c, sysproc.c.
- Semantics & edge cases:
  - Threads should share address space and file descriptors if implementing true threads.
  - thread_join must wait for thread termination and collect return value.
  - thread_exit should cleanly free thread resources (stack).
- Test:
  - t_threads: create multiple threads that increment a shared counter with synchronization (e.g., user spinlocks), validate final value.

Part D — Userspace spinlocks
- Behavior:
  - Implement a simple spinlock that works in user mode (busy-waiting).
  - API example:
    - void uspin_init(uspinlock_t *lk);
    - void uspin_lock(uspinlock_t *lk);
    - void uspin_unlock(uspinlock_t *lk);
- Implementation notes:
  - Use atomic operations (xchg) available in xv6 userland or inline assembly.
  - Cannot rely on kernel sleep — spinning is required.
  - Ensure memory barriers where needed.
- Files to modify:
  - Add a user library file (e.g., uspin.c / ulib.c) with implementations.
  - user.h: add prototypes and type definitions.
- Test:
  - t_lock: spawn many threads that increment a shared counter using user spinlock; verify correctness and measure contention behavior.

Makefile and build
- Add any new object files to the kernel/user build lists:
  - Example: in Makefile add barrier.o or uspin.o where appropriate.
- Ensure user test programs are compiled and linked into the fs image for qemu test runs.
- Commands:
  - make            # build kernel and userland
  - make qemu-nox   # run without GUI
  - Run tests inside xv6 shell, e.g.:
    - t_waitpid
    - t_barrier
    - t_threads
    - t_lock

Testing and debugging workplan
- Add printk-style debugging (cprintf in kernel, printf in user programs).
- Use qemu-nox and serial output to view test logs.
- When debugging sleeps/wakeups, check that locks are held appropriately and wakeup targets the correct channel.
- For thread/address-space bugs, ensure stacks do not overlap and are page-aligned.
- For waitpid, carefully inspect parent/child relationships and process states ZOMBIE/RUNNING/SLEEPING.

Checklist before submission
- All syscalls and user APIs compile cleanly.
- Kernel builds without warnings from modified files.
- Tests exist for each part and pass inside xv6.
- Makefile updated to include new sources.
- No large binaries committed (use .gitignore).
