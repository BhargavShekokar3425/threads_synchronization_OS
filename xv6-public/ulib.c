#include "types.h"
#include "stat.h"
#include "fcntl.h"
#include "user.h"
#include "x86.h"

char*
strcpy(char *s, const char *t)
{
  char *os;

  os = s;
  while((*s++ = *t++) != 0)
    ;
  return os;
}

int
strcmp(const char *p, const char *q)
{
  while(*p && *p == *q)
    p++, q++;
  return (uchar)*p - (uchar)*q;
}

uint
strlen(const char *s)
{
  int n;

  for(n = 0; s[n]; n++)
    ;
  return n;
}

void*
memset(void *dst, int c, uint n)
{
  stosb(dst, c, n);
  return dst;
}

char*
strchr(const char *s, char c)
{
  for(; *s; s++)
    if(*s == c)
      return (char*)s;
  return 0;
}

char*
gets(char *buf, int max)
{
  int i, cc;
  char c;

  for(i=0; i+1 < max; ){
    cc = read(0, &c, 1);
    if(cc < 1)
      break;
    buf[i++] = c;
    if(c == '\n' || c == '\r')
      break;
  }
  buf[i] = '\0';
  return buf;
}

int
stat(const char *n, struct stat *st)
{
  int fd;
  int r;

  fd = open(n, O_RDONLY);
  if(fd < 0)
    return -1;
  r = fstat(fd, st);
  close(fd);
  return r;
}

int
atoi(const char *s)
{
  int n;

  n = 0;
  while('0' <= *s && *s <= '9')
    n = n*10 + *s++ - '0';
  return n;
}

void*
memmove(void *vdst, const void *vsrc, int n)
{
  char *dst;
  const char *src;

  dst = vdst;
  src = vsrc;
  while(n-- > 0)
    *dst++ = *src++;
  return vdst;
}

/////////// New additions for Parts D and E of threads lab/////////
void initiateLock(struct lock* l) {
    l->lockvar = 0;
    l->isInitiated = 1;
}

void acquireLock(struct lock* l) {
    // Check if the lock has been initialized
    if(!l->isInitiated) {
        return; // or you could panic/error
    }
    
    // Use atomic xchg to acquire the lock
    // xchg atomically exchanges the value at &l->lockvar with 1
    // and returns the old value. If old value was 0, we got the lock.
    // If old value was 1, someone else has it, so we spin.
    // Cast to volatile uint* to match xchg's expected type
    while(xchg((volatile uint*)&l->lockvar, 1) != 0)
        ; // spin until we acquire the lock
}

void releaseLock(struct lock* l) {
    // Check if the lock has been initialized
    if(!l->isInitiated) {
        return; // or you could panic/error
    }
    
    // Release the lock by setting it to 0
    // We use xchg to ensure atomicity
    // Cast to volatile uint* to match xchg's expected type
    xchg((volatile uint*)&l->lockvar, 0);
}

void initiateCondVar(struct condvar* cv) {

}

void condWait(struct condvar* cv, struct lock* l) {

}

void broadcast(struct condvar* cv) {

}

void signal(struct condvar* cv) {

}

void semInit(struct semaphore* s, int initVal) {

}

void semUp(struct semaphore* s) {

}

void semDown(struct semaphore* s) {

}

/////////// End of New additions for Parts D and E of threads lab/////////
