#ifndef THREADS_THREAD_H
#define THREADS_THREAD_H
#define FD_MAX 128 // 프로세스당 동시에 열 수 있는 최대 파일 개수.

#include <debug.h>
#include <list.h>
#include <stdint.h>
#include "threads/synch.h"

/* States in a thread's life cycle. */
enum thread_status
  {
    THREAD_RUNNING,     /* Running thread. */
    THREAD_READY,       /* Not running but ready to run. */
    THREAD_BLOCKED,     /* Waiting for an event to trigger. */
    THREAD_DYING        /* About to be destroyed. */
  };

/* Thread identifier type.
   You can redefine this to whatever type you like. */
typedef int tid_t;
#define TID_ERROR ((tid_t) -1)          /* Error value for tid_t. */

/* Thread priorities. */
#define PRI_MIN 0                       /* Lowest priority. */
#define PRI_DEFAULT 31                  /* Default priority. */
#define PRI_MAX 63                      /* Highest priority. */

struct child_status{
   tid_t tid;
   int exit_status;
   bool is_exited;

   struct semaphore wait_sema;// 자식 종료 대기용
   struct semaphore load_sema;// 자식 로드 대기용
   bool load_success; // 자식의 로드 성공 여부
   struct list_elem elem; // 부모의 child_list에 연결하기 위한 리스트 요소
};
/* A kernel thread or user process.

   Each thread structure is stored in its own 4 kB page.  The
   thread structure itself sits at the very bottom of the page
   (at offset 0).  The rest of the page is reserved for the
   thread's kernel stack, which grows downward from the top of
   the page (at offset 4 kB).  Here's an illustration:

        4 kB +---------------------------------+
             |          kernel stack           |
             |                |                |
             |                |                |
             |                V                |
             |         grows downward          |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             +---------------------------------+
             |              magic              |
             |                :                |
             |                :                |
             |               name              |
             |              status             |
        0 kB +---------------------------------+

   The upshot of this is twofold:

      1. First, `struct thread' must not be allowed to grow too
         big.  If it does, then there will not be enough room for
         the kernel stack.  Our base `struct thread' is only a
         few bytes in size.  It probably should stay well under 1
         kB.

      2. Second, kernel stacks must not be allowed to grow too
         large.  If a stack overflows, it will corrupt the thread
         state.  Thus, kernel functions should not allocate large
         structures or arrays as non-static local variables.  Use
         dynamic allocation with malloc() or palloc_get_page()
         instead.

   The first symptom of either of these problems will probably be
   an assertion failure in thread_current(), which checks that
   the `magic' member of the running thread's `struct thread' is
   set to THREAD_MAGIC.  Stack overflow will normally change this
   value, triggering the assertion. */
/* The `elem' member has a dual purpose.  It can be an element in
   the run queue (thread.c), or it can be an element in a
   semaphore wait list (synch.c).  It can be used these two ways
   only because they are mutually exclusive: only a thread in the
   ready state is on the run queue, whereas only a thread in the
   blocked state is on a semaphore wait list. */
struct thread
  {
    /* Owned by thread.c. */
    tid_t tid;                          /* Thread identifier. */
    enum thread_status status;          /* Thread state. */
    char name[16];                      /* Name (for debugging purposes). */
    uint8_t *stack;                     /* Saved stack pointer. */
    int priority;                       /* Priority. */
    struct list_elem allelem;           /* List element for all threads list. */

    /* Shared between thread.c and synch.c. */
    struct list_elem elem;              /* List element. */

// 유저 프로그램 전용 필드
#ifdef USERPROG
    /* Owned by userprog/process.c. */
    uint32_t *pagedir;                  /* Page directory. */

    // 부모-자식, 동기화 관련 필드들
    struct thread *parent;              /* 부모 프로세스 포인터(자식이 종료된 후 부모를 깨울 때의 역참조를 위해) */
    //struct list child_list;             /* 생성한 자식 프로세스들의 리스트(wait(child_pid)호출 시에 실제 자식인지 확인하고 관리하기 위해) */
    struct list child_list;               /* 생성한 자식들의 'struct child_status' 리스트 */
    struct child_status *cp;              /* 자신이 속한 상태 상자(struct child_status)를 가리키는 포인터 (Child Pointer)*/
    //struct list_elem child_elem;        /* child_list에 들어가기 위한 list_elem(자식 스레드 간의 연결 고리) */

    // 부모가 wait(child_pid)를 호출했을 때, 자식 프로세스가 아직 끝나지 않았다면
    // 부모는 자식이 끝날 때까지 Blocking 상태여야 하므로,
    // 부모를 잠재우고(down), 자식이 종료될 때 깨워주기(up)위해 사용하는 세마포어(wait_sema)
    //struct semaphore wait_sema;         /* 부모가 자식을 wait 할 때 사용하는 세마포어 */
    
    // 자식은 죽었지만, 부모가 아직 exit status를 읽지 않아서, 
    // 이걸 읽고 sema up 할 때까지 자식이 스스로 소멸하지 않고 
    // 대기하기 위해 사용하는 세마포어(exit_sema)
    //struct semaphore exit_sema;         /* 부모가 자식의 exit status를 읽기 위해 사용하는 세마포어 */

    // 부모가 process_execute()의 thread_create()에서 자식을 만들면
    // 자식 프로세스가 프로그램 코드를 제대로 load 하는 데 시간이 걸리기 때문에
    // 자식의 load가 끝나기 전에 부모가 다음 코드를 실행하거나, 자식의 상태를 조작하지 못하도록
    // 자식의 load가 완전히 끝날 때까지 sema down하고 
    // 로드가 성공하면 load_success를 true로 조정하고 sema up을 해준다. 

    //struct semaphore load_sema;         /* 자식이 메모리에 완전히 로드될 때까지 부모가 기다릴 때 사용하는 세마포어 */
    //bool load_success;                  /* 자식 프로세스의 로드 성공 여부 */
    
    // wait으로 반환할 자식의 종료 코드
    int exit_status;                    /* 자식이 종료될 때 부모에게 전달할 종료 상태 */


    // file descriptor 관리 관련 필드들

    // 특정 fd로 파일 객체를 매핑하기 위한 fd table
    struct file *fd_table[FD_MAX];
    struct file *exec_file; /* 실행줄인 파일 (쓰기 금지)*/
#endif

    /* Owned by thread.c. */
    unsigned magic;                     /* Detects stack overflow. */
  };

/* If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "-o mlfqs". */
extern bool thread_mlfqs;

void thread_init (void);
void thread_start (void);

void thread_tick (void);
void thread_print_stats (void);

typedef void thread_func (void *aux);
tid_t thread_create (const char *name, int priority, thread_func *, void *);

void thread_block (void);
void thread_unblock (struct thread *);

struct thread *thread_current (void);
tid_t thread_tid (void);
const char *thread_name (void);
/* 추가 구현 함수 */
// (userprog/process.c의 process_execute()에서 tid를 통해 스레드 구조체에 접근하기 위함)
struct thread *get_thread(tid_t tid);

struct child_status *get_child_status(tid_t child_tid);

void thread_exit (void) NO_RETURN;
void thread_yield (void);

/* Performs some operation on thread t, given auxiliary data AUX. */
typedef void thread_action_func (struct thread *t, void *aux);
void thread_foreach (thread_action_func *, void *);

int thread_get_priority (void);
void thread_set_priority (int);

int thread_get_nice (void);
void thread_set_nice (int);
int thread_get_recent_cpu (void);
int thread_get_load_avg (void);

#endif /* threads/thread.h */
