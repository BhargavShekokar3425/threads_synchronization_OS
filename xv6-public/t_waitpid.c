#include "user.h"

int main()
{
  int ret;
  int retwait;
  
  ret = fork();
  if(ret == 0)
  {
    sleep(10);
    exit();
  }
  else
  {
    // Try to waitpid on a non-existent child (wrong PID)
    retwait = waitpid(ret+1);
    printf(1, "return value of wrong waitpid %d\n", retwait);

    // Now waitpid on the correct child
    retwait = waitpid(ret);
    printf(1, "return value of correct waitpid %d\n", retwait);
    
    // Try to wait again - should return -1 since child already reaped
    retwait = wait();
    printf(1, "return value of wait %d\n", retwait);
    
    printf(1, "child reaped\n");
    exit();
  }
}

