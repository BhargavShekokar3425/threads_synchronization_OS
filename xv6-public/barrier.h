/*----------xv6 sync lab----------*/
#ifndef BARRIER_H
#define BARRIER_H

// Initialize the kernel barrier with count n (single-use).
void barrier_init(int n);

// Wait at the kernel barrier until the configured number of processes arrive.
// This is a kernel-side call and returns no value.
void barrier_check(void);

#endif // BARRIER_H
/*----------xv6 sync lab end----------*/
