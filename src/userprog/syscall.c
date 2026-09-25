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
  // 가능한 한 드물게 사용해야한다. 
  // 발생 가능한 교착 상태(deadlock) 상황 등에 대한 일부 정보를 잃게 되기 때문이다. 
  shutdown_power_off();
  return;
}

void sys_exit(int exit_status){
  // void exit(int status)
  // 현재의 유저 프로그램을 종료하고 커널에 status를 반환한다.
  // 만일 process의 부모가 wait 상태로 이 프로그램의 종료를 기다리고 있다면-> 아래 wait()에서 이어서 설명. 
  // 관례적으로, 0의 상태가 성공을 가리키고, nonzero value는 에러를 가리킨다. 
  struct thread *curr = thread_current(); // 현재 실행 중인 스레드를 가져와서
  
  // 종료 상태를 현재 스레드 구조체에 저장.
  curr-> status = exit_status;

  // "Process Name: exit(exit status)"를 출력.
  printf("%s: exit(%d)\n", curr->name, exit_status);
  thread_exit(); // 커널의 스레드 종료 함수 호출. 
}

tid_t sys_exec(const char *cmd_line){
  // cmd_line에 주어진 이름을 가진 실행 파일을 실행하고,
  // 전달된 인자들을 넘겨준 뒤,
  // 새 프로세스의 pid를 반환한다.
  // 어떤 이유로든 프로그램이 로드되거나 실행될 수 없는 경우,
  // 유효하지 않은 pid인 -1을 반환해야 한다.
  // 따라서 부모 프로세스는 자식 프로세스가 실행 파일을 성공적으로 
  // 로드했는지의 여부를 알 때까지, exec에서 리턴 불가능하며,
  // 이를 보장하기 위해 적절한 동기화를 사용해야 한다.
  return 0;
}

int sys_wait(tid_t tid){
  /*
  자식 프로세스 pid를 기다리며 자식의 종료 상태(exit status)를 가져옴.
  만약 pid가 여전히 살아있다면, 종료될 때까지 기다린다.
  그런 다음 pid가 exit에 전달했던 상태값을 반환.
  만약 pin가 exit()을 호출하지 않고 커널에 의해 종료된 경우 
  wait(pid)는 반드시 -1을 반환해야 함.
  부모 프로세스가 wait을 호출할 시점에 이미 종료된 자식 프로세스를 기다리는 것도 적법하지만,
  커널은 부모가 자식의 종료 상태를 가져오거나 자식이 커널에 의해 종료되었음을 알 수 있도록 여전히 허용해야함.

  *다음 조건 중 하나라도 참이면 wait()은 실패하고 즉시 -1을 반환해야 함*
  - pid가 호출하는 직계 자식을 가리키지 않는 경우.
    pid가 호출하는 프로세스의 직계 자식인 경우는 오직 호출하는 프로세스가 exec의 성공적인 호출 결과로
    pid를 리턴 받았을 때뿐이다.
    (다음은 간소화된 pintos의 규칙이다.)
    자식은 상속되지 않는다는 점에 유의. 즉, A가 자식 B를 생성하고 B가 자식 프로세스 C를 생성한 경우,
    B가 죽었더라도 A는 C를 기다릴 수 없다.(원래는 부모의 부모가 해당 자식을 입약하여 리핑된다.)
    프로세스 A가 wait(C)를 호출하면 반드시 실패해야 함.
    마찬가지로, 고아(orphaned) 프로세스는 부모 프로세스가 먼저 종료되더라도 새로운 부모에게 할당되지 않음. 
  - wait()을 호출하는 프로세스가 이미 해당 pid에 대해 wait()을 호출한 적이 있는 경우.
    즉 프로세스는 주어진 자식에 대해 최대 한 번만 기다릴 수 있다.

  프로세스는 임의의 수의 자식을 생성할 수 있고, 어느 순서로든 기다릴 수 있으며, 
  일부 또는 모든 자식을 기다리지 않고 종료할 수도 있다.
  설계할 때 wait()이 발생할 수 있는 모든 방식을 고려해야 한다.
  
  부모가 자식을 한 번이라도 기다리든 말든, 그리고 자식이 부모보다 먼저 종료되든 나중에 종료되든 상관없이
  struct thread를 포함한 모든 자원은 반드시 해제되어야 한다. 
  
  초기프로세스가 종료될 때까지 Pintos가 종료되지 않도록 보장해야 한다. 
  제공된 pintos 코드는 threads/init.c의 main()에서 
  userprog/process.c의 process_wait()을 호출하여 이를 수행하려고 시도한다.
  함수 상단의 주석에 따라 process_wait()을 구현한 다음, 
  process_wait()을 활용해 wait system call을 구현할 것을 권장함.

  */
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
  /* 
  열려 있는 fd에 buffer로부터 size 바이트만큼을 쓴다.
  실제로 쓰여진 바이트 수를 반환하며 일부 바이트를 쓸 수 없는 경우 size보다 작을 수 있다.
  (파일을 동적으로 늘리는 기능이 pintos에는 없기 떄문(원래 OS에는 있음))
  파일의 끝(EOF)를 넘어 쓰기를 시도하면 원래는 파일이 확장되어야 하지만,
  기본 파일 시스템에는 파일 크기 확장 기능이 구현되어 있지 않다.
  
  따라서 파일 끝까지 가능한 한 많은 바이트를 쓰고 실제로 쓰여진 바이트 수를 반환하거나,
  바이트를 전혀 쓸 수 없는 경우 0을 반환하는 것이 예상되는 메커니즘이다.

  fd 1은 콘솔에 출력한다. 콘솔에 쓰는 코드는, 적어도 수백 바이트를 넘지 않는 한,
  putbuf()의 호출로 buffer의 모든 내용을 작성해야 한다.
  (더 큰 버퍼를 나누어 쓰는 것은 허용된다.)
   // 멀티스레딩(동시성) 환경에서의 Race Condition/Interleaving을 막기 위해서 
  그렇지 않으면 서로 다른 프로세스가 출력하는 텍스트 줄들이 콘솔에 뒤섞여(interleaved)
  사람인 독자와 채점 스크립트 모두를 혼란스럽게 만들 수 있다. 

  void putbuf(const char *buffer, size_t size); // size_t는 unsigned int이다.
  // 커널 내부에서 버퍼에 들어 있는 문자열 데이터를 화면에 통째로 출력해주는 함수.(lib/kernel/console.h)
  */
  if(fd==1){
    putbuf(buffer, size);
    return size;
  }
  else if(fd==0){
    return -1;
  }
  else if(fd>=2){ // prj1-2
    // fd 테이블 내에서 현재 fd를 찾아보고, 
    // 없다면 -1 리턴, 
    // 있다면 file_write() 호출 및 쓰여진 바이트 수 반환
    struct file *file = thread_current()->fd_table[fd];
    if(file == NULL) return -1;
    return file_write(file, buffer, size);
  }
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
  printf ("system call!\n");
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
    // void halt (void) NO_RETURN;
      sys_halt();
      break;
    }
    case SYS_EXIT: {
      // void exit(int status)
      check_addr(f->esp+4);
      int exit_status = *(int*)(f->esp +4);
      sys_exit(exit_status);
      break;
    }
    case SYS_EXEC: { 
      // pid_t exec (const char *file);
      check_addr(f->esp+4);
      int file_name = *(int*)(f->esp+4);
      sys_exec(file_name);
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
      // int write (int fd, const void *buffer, unsigned length);
      check_addr(f->esp+4);
      check_addr(f->esp+8);
      check_addr(f->esp+12);

      int fd = *(int*)(f->esp+4);
      // (f->esp+8)에는 문자열의 주소값이 담기고 이를 4바이트 정수형 주소(uint32_t *)로 읽는다.
      // 해당 값을 스택에서 꺼내오고 (*)
      // write 두 번째 인자인 buffer의 형으로 casting(const void*)
      const void* buffer = (const void*)*(uint32_t *)(f->esp+8);
      unsigned size = *(unsigned*)(f->esp+12);

      int ret = sys_write(fd, buffer, size);
      f-> eax = ret;
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
  //thread_exit ();
}