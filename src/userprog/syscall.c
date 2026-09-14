#include "userprog/syscall.h"
#include "threads/vaddr.h"
#include <stdio.h>
#include "userprog/pagedir.h" // for pagedir_get_page()
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "devices/shutdown.h"

static void syscall_handler (struct intr_frame *);

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

// 전달받은 포인터가 유효한 유저 메모리 주소인지 확인하는 함수
void check_addr(const void *vaddr){
  // 1. 널포인터인지
  // 2. 유저 가장 메모리 영역(PHYS_BASE 미만)이 맞는지
  // 3. 페이지 디렉토리에 실제로 매핑된 페이지가 존재하는지
  if(vaddr== NULL || !is_user_vaddr(vaddr) || pagedir_get_page(thread_current() -> pagedir, vaddr)== NULL)
    sys_exit(-1); // 유효하지 않으면 종료. 
  return;
}

void sys_halt(void){
  // shutdown_power_off()를 호출함으로써 pintos를 종료시킨다.
  // 
  shutdown_power_off();
  return;
}

void sys_exit(int exit_status){
  // void exit(int status)
  // 현재의 유저 프로그램을 종료하고 커널에 status를 반환한다.
  // 만일 process의 부모가 wait 상태로 이 프로그램의 종료를 기다리고 있다면,
  // 이 상태가 반환될 것이다(?)
  // 관례적으로, 0의 상태가 성공을 가리키고, nonzero value는 에러를 가리킨다. 
  struct thread *curr = thread_current(); // 현재 실행 중인 스레드를 가져와서
  
  // 종료 상태를 현재 스레드 구조체에 저장.
  curr-> status = exit_status;

  // "Process Name: exit(exit status)"를 출력.
  printf("%s: exit(%d)\n", curr->name, exit_status);
  thread_exit(); // 커널의 스레드 종료 함수 호출. 
}

tid_t sys_exec(const char *cmd_line){
  // 
  return 0;
}

int sys_wait(tid_t tid){

  return 0;
}

int sys_fibonacci(int n){
  return 0;
}

int sys_max_of_four_int(int a, int b, int c, int d){
  return 0;
}

bool sys_create(const char *file, unsigned initial_size){
  return true;
}

bool sys_remove(const char *file){
  return true;
}

int sys_open(const char *file){
  return 0;
}

int sys_filesize(int fd){
  return 0;
}

int sys_read(int fd, void *buffer, unsigned size){
  return 0;
}

int sys_write(int fd, const void *buffer, unsigned size){
  return 0;
}

void sys_seek(int fd, unsigned position){
  return;
}

unsigned sys_tell(int fd){
  return 0;
}

void sys_close(int fd){
  return;
}

/* $int 0x30을 불렀을 때 호출되는 syscall_handler이다. */
static void
syscall_handler (struct intr_frame *f UNUSED) 
{
  /* 현재 스택의 상태는 다음과 같다. */
  /*                     ex. write(fd, buffer, size);
    esp+16 |  arg3  |     
    esp+12 |  arg2  |        esp+12 |  size  |
    esp+8  |  arg1  |        esp+8  | buffer |
    esp+4  |  arg0  |        esp+4  |   fd   |
    esp -> | number |        esp -> | number |
  */


  // 현재 스택에 쌓여있는 값 중 esp에 저장된 syscall num을 가져온다. 
  check_addr(f->esp);
  int syscall_num = *(int*)f->esp;

  // 유효한 시스템 콜 번호인지 확인(0~14(max_of_four_int와 fibonacci도 구현한다는 전제하에))
  if(syscall_num<0 || syscall_num>14)
    sys_exit(-1);

  // 시스템 콜 번호에 따라 분기
  switch(syscall_num){
    case SYS_HALT: {

      break;
    }
    case SYS_EXIT: {
      // void exit(int status)
      int exit_status = *(int*)(f->esp +4);
      sys_exit(exit_status);
      break;
    }
    case SYS_EXEC: {

      break;
    }
    case SYS_WAIT: {

      break;
    }
    case SYS_CREATE: {

      break;
    }
    case SYS_REMOVE: {

      break;
    }
    case SYS_OPEN: {

      break;
    }
    case SYS_FILESIZE: {

      break;
    }
    case SYS_READ: {

      break;
    }
    case SYS_WRITE: {

      break;
    }
    case SYS_SEEK: {

      break;
    }
    case SYS_TELL: {

      break;
    }
    case SYS_CLOSE: {

      break;
    }
    case SYS_FIBONACCI:{

      break;
    }
    case SYS_MAX_OF_FOUR_INT: {

      break;
    }

  }

  //printf ("system call!\n");
  //thread_exit ();
}