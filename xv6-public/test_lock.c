#include "user.h"

struct lock counter_lock;
int shared_counter = 0;

// Thread function WITH lock protection
void* increment_with_lock(void* arg) {
    int iterations = *(int*)arg;
    for(int i = 0; i < iterations; i++) {
        acquireLock(&counter_lock);
        shared_counter++;
        releaseLock(&counter_lock);
    }
    thread_exit();
    return 0;
}

// Thread function WITHOUT lock protection (to show race condition)
void* increment_without_lock(void* arg) {
    int iterations = *(int*)arg;
    for(int i = 0; i < iterations; i++) {
        // No lock - this will cause race conditions
        shared_counter++;
    }
    thread_exit();
    return 0;
}

int main() {
    uint tid1, tid2;
    int iterations = 100000;
    
    printf(1, "\n=== Testing Userspace Spinlocks ===\n\n");
    
    // Test 1: With locks (should work correctly)
    printf(1, "Test 1: Incrementing with locks (2 threads, %d iterations each)\n", iterations);
    initiateLock(&counter_lock);
    shared_counter = 0;
    
    thread_create(&tid1, increment_with_lock, &iterations);
    thread_create(&tid2, increment_with_lock, &iterations);
    
    thread_join(tid1);
    thread_join(tid2);
    
    int expected = 2 * iterations;
    printf(1, "Expected value: %d\n", expected);
    printf(1, "Actual value:   %d\n", shared_counter);
    
    if(shared_counter == expected) {
        printf(1, "✓ PASS: Locks working correctly!\n\n");
    } else {
        printf(1, "✗ FAIL: Race condition detected!\n\n");
    }
    
    // Test 2: Without locks (to demonstrate race condition)
    printf(1, "Test 2: Incrementing WITHOUT locks (2 threads, %d iterations each)\n", iterations);
    shared_counter = 0;
    
    thread_create(&tid1, increment_without_lock, &iterations);
    thread_create(&tid2, increment_without_lock, &iterations);
    
    thread_join(tid1);
    thread_join(tid2);
    
    printf(1, "Expected value: %d\n", expected);
    printf(1, "Actual value:   %d\n", shared_counter);
    
    if(shared_counter != expected) {
        printf(1, "✓ Expected: Race condition detected (locks are needed!)\n\n");
    } else {
        printf(1, "? Note: Got correct value by chance (race conditions are non-deterministic)\n\n");
    }
    
    // Test 3: Multiple shared variables protected by same lock
    printf(1, "Test 3: Multiple shared variables with same lock\n");
    struct lock data_lock;
    int var1 = 0, var2 = 0;
    
    initiateLock(&data_lock);
    
    void* update_vars(void* arg) {
        for(int i = 0; i < 50000; i++) {
            acquireLock(&data_lock);
            var1++;
            var2 += 2;
            releaseLock(&data_lock);
        }
        thread_exit();
        return 0;
    }
    
    thread_create(&tid1, update_vars, 0);
    thread_create(&tid2, update_vars, 0);
    
    thread_join(tid1);
    thread_join(tid2);
    
    printf(1, "var1 (expected 100000): %d\n", var1);
    printf(1, "var2 (expected 200000): %d\n", var2);
    
    if(var1 == 100000 && var2 == 200000) {
        printf(1, "✓ PASS: Multiple variables protected correctly!\n\n");
    } else {
        printf(1, "✗ FAIL: Incorrect values!\n\n");
    }
    
    printf(1, "=== All tests completed ===\n");
    exit();
}