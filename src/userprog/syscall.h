#include <stdbool.h>
#include "threads/thread.h"
#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

void syscall_init (void);
void check_addr(const void*);
void sys_halt(void);
void sys_exit(int);
tid_t sys_exec(const char*);
int sys_wait(tid_t);
int sys_fibonacci(int);
int sys_max_of_four_int(int, int, int, int);

bool sys_create(const char*, unsigned);
bool sys_remove(const char *);
int sys_open(const char *);
int sys_filesize(int);
int sys_read(int, void *, unsigned);
int sys_write(int, const void *, unsigned);
void sys_seek(int, unsigned);
unsigned sys_tell(int);
void sys_close(int);


#endif /* userprog/syscall.h */
