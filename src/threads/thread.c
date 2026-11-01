#include "threads/thread.h"
#include <debug.h>
#include <stddef.h>
#include <random.h>
#include <stdio.h>
#include <string.h>
#include "threads/flags.h"
#include "threads/interrupt.h"
#include "threads/intr-stubs.h"
#include "threads/palloc.h"
#include "threads/switch.h"
#include "threads/synch.h"
#include "threads/vaddr.h"
#include "devices/timer.h"
#ifdef USERPROG
#include "userprog/process.h"
#endif

/* Random value for struct thread's `magic' member.
   Used to detect stack overflow.  See the big comment at the top
   of thread.h for details. */
#define THREAD_MAGIC 0xcd6abf4b

/* List of processes in THREAD_READY state, that is, processes
   that are ready to run but not actually running. */
static struct list ready_list;
/* 잠든 스레드들을 관리할 수면 큐(wakeup_tick 오름차순으로 정렬) */
/* static이라 thread.c 밖에서는 이름으로 접근 불가하여
   timer.c는 thread_sleep(), thread_wake_up을 통해 접근한다. */
static struct list sleep_list;

/* List of all processes.  Processes are added to this list
   when they are first scheduled and removed when they exit. */
static struct list all_list;

/* Idle thread. */
static struct thread *idle_thread;

/* Initial thread, the thread running init.c:main(). */
static struct thread *initial_thread;

/* Lock used by allocate_tid(). */
static struct lock tid_lock;

/* Stack frame for kernel_thread(). */
struct kernel_thread_frame 
  {
    void *eip;                  /* Return address. */
    thread_func *function;      /* Function to call. */
    void *aux;                  /* Auxiliary data for function. */
  };

/* Statistics. */
static long long idle_ticks;    /* # of timer ticks spent idle. */
static long long kernel_ticks;  /* # of timer ticks in kernel threads. */
static long long user_ticks;    /* # of timer ticks in user programs. */

/* Scheduling. */
#define TIME_SLICE 4            /* # of timer ticks to give each thread. */
static unsigned thread_ticks;   /* # of timer ticks since last yield. */

#ifndef USERPROG
bool thread_prior_aging;
#endif

/* If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "-o mlfqs". */
bool thread_mlfqs;

static void kernel_thread (thread_func *, void *aux);

static void idle (void *aux UNUSED);
static struct thread *running_thread (void);
static struct thread *next_thread_to_run (void);
static void init_thread (struct thread *, const char *name, int priority);
static bool is_thread (struct thread *) UNUSED;
static void *alloc_frame (struct thread *, size_t size);
static void schedule (void);
void thread_schedule_tail (struct thread *prev);
static tid_t allocate_tid (void);

//void thread_check_preempt (void);


/* Initializes the threading system by transforming the code
   that's currently running into a thread.  This can't work in
   general and it is possible in this case only because loader.S
   was careful to put the bottom of the stack at a page boundary.

   Also initializes the run queue and the tid lock.

   After calling this function, be sure to initialize the page
   allocator before trying to create any threads with
   thread_create().

   It is not safe to call thread_current() until this function
   finishes. */
void
thread_init (void) 
{
  ASSERT (intr_get_level () == INTR_OFF);

  lock_init (&tid_lock);
  // init 호출 없이 쓰면 head/tail이 0으로 채워진 상태라 순회할 때 NULL을 따라가 커널이 죽음
  list_init (&ready_list);
  list_init (&all_list);
  list_init(&sleep_list); // 스레드 하나라도 잠들기 전에 초기화해야함.

  /* Set up a thread structure for the running thread. */
  initial_thread = running_thread ();
  init_thread (initial_thread, "main", PRI_DEFAULT);
  initial_thread->status = THREAD_RUNNING;
  initial_thread->tid = allocate_tid ();
}

/* Starts preemptive thread scheduling by enabling interrupts.
   Also creates the idle thread. */
void
thread_start (void) 
{
  /* Create the idle thread. */
  struct semaphore idle_started;
  sema_init (&idle_started, 0);
  thread_create ("idle", PRI_MIN, idle, &idle_started);

  /* Start preemptive thread scheduling. */
  intr_enable ();

  /* Wait for the idle thread to initialize idle_thread. */
  sema_down (&idle_started);
}

/* Called by the timer interrupt handler at each timer tick.
   Thus, this function runs in an external interrupt context. */
void
thread_tick (void) 
{
  struct thread *t = thread_current ();

  /* Update statistics. */
  if (t == idle_thread)
    idle_ticks++;
#ifdef USERPROG
  else if (t->pagedir != NULL)
    user_ticks++;
#endif
  else
    kernel_ticks++;

  // 한 스레드가 TIME_SLICE를 다 쓰면 인터럽트가 끝날 때 양보 (round-robin)
  /* Enforce preemption. */
  if (++thread_ticks >= TIME_SLICE)
    intr_yield_on_return ();

  // 매 틱마다 깨어날 스레드가 있는지 확인. 
  thread_wake_up();

// aging의 경우 -aging 옵션이 켜졌을 때만 매 틱 실행한다. 
#ifndef USERPROG
  if(thread_prior_aging == true)
    thread_aging();
#endif
}

/* Prints thread statistics. */
void
thread_print_stats (void) 
{
  printf ("Thread: %lld idle ticks, %lld kernel ticks, %lld user ticks\n",
          idle_ticks, kernel_ticks, user_ticks);
}

/* Creates a new kernel thread named NAME with the given initial
   PRIORITY, which executes FUNCTION passing AUX as the argument,
   and adds it to the ready queue.  Returns the thread identifier
   for the new thread, or TID_ERROR if creation fails.

   If thread_start() has been called, then the new thread may be
   scheduled before thread_create() returns.  It could even exit
   before thread_create() returns.  Contrariwise, the original
   thread may run for any amount of time before the new thread is
   scheduled.  Use a semaphore or some other form of
   synchronization if you need to ensure ordering.

   The code provided sets the new thread's `priority' member to
   PRIORITY, but no actual priority scheduling is implemented.
   Priority scheduling is the goal of Problem 1-3. */
tid_t
thread_create (const char *name, int priority,
               thread_func *function, void *aux) 
{
  struct thread *t;
  struct kernel_thread_frame *kf;
  struct switch_entry_frame *ef;
  struct switch_threads_frame *sf;
  tid_t tid;

  ASSERT (function != NULL);

  /* Allocate thread. */
  t = palloc_get_page (PAL_ZERO);
  if (t == NULL)
    return TID_ERROR;

  /* Initialize thread. */
  init_thread (t, name, priority);
  tid = t->tid = allocate_tid ();
  #ifdef USERPROG
  // t의 자식 부모 관계 설정(현재의 current는 부모)
  t->parent = thread_current();
  #endif

  /* Stack frame for kernel_thread(). */
  kf = alloc_frame (t, sizeof *kf);
  kf->eip = NULL;
  kf->function = function;
  kf->aux = aux;

  /* Stack frame for switch_entry(). */
  ef = alloc_frame (t, sizeof *ef);
  ef->eip = (void (*) (void)) kernel_thread;

  /* Stack frame for switch_threads(). */
  sf = alloc_frame (t, sizeof *sf);
  sf->eip = switch_entry;
  sf->ebp = 0;

  // Ready Queue에 들어간 후, 스케줄러가 타이머 인터럽트를 통해
  // CPU를 부모 스레드에게서 빼앗아 자식에게 넘겨주면 current가 자식이 되는 것. 
  /* Add to run queue. */
  thread_unblock (t);
  // 새로 높은 스레드가 생겼을 수 있으므로
  thread_check_preempt();

  return tid;
}

struct thread*
get_thread(tid_t tid)
{
  struct list_elem *e;

  for(e = list_begin(&all_list); e!= list_end(&all_list);
      e = list_next(e))
      {
        struct thread *t = list_entry(e, struct thread, allelem);
        if(t->tid == tid)
          return t;
      }
  return NULL;
}

#ifdef USERPROG
struct child_status *
get_child_status(tid_t child_tid)
{
  struct thread *cur = thread_current();
  struct list_elem *e;

  for(e=list_begin(&cur->child_list); 
      e!=list_end(&cur->child_list); e = list_next(e)){
        struct child_status *st = list_entry(e, struct child_status, elem);
        if(st->tid==child_tid)
          return st;
      }
  return NULL;
}
#endif

/* a가 b보다 먼저 깨어나야 하면 true (sleep_list를 정렬하기 위함.)*/
static bool
wakeup_less(const struct list_elem *a, const struct list_elem *b,
            void *aux UNUSED)
{
  // a가 b보다 엄격히 먼저 깨어날 때만 true..
  // (<) 같은 시각이면 false라서 같은 시각에 이미 잠든 스레드들 뒤에 들어감.(먼저 잠든 순서가 유지됨.)
  return list_entry(a, struct thread, sleep_elem)->wakeup_tick
       < list_entry(b, struct thread, sleep_elem)->wakeup_tick;
}

void 
thread_sleep(int64_t wakeup_tick)
{
  // 잠들 스레드(timer_sleep()을 부른 자기 자신)
  struct thread *cur = thread_current();
  // interrupt를 끄기 전 상태를 기억해둘 변수
  enum intr_level old_level;
  // idle 스레드가 아닌 경우에만 재움. 
  // CPU는 모든 스레드가 자거나 기다려서 ready_list가 비면
  // next_thread_to_run()이 idle을 돌려줌
  // idle은 다음 인터럽트가 올 때까지 std;glt로 CPU를 쉬게하는 역할.
  // 그런 idle을 sleep_list에 넣고 재우면 실행할 게 없을 때 CPU가 돌릴 대상이 사라지고
  // 나중에 깨울 때 idle이 ready_list에 들어가서
  // idle은 ready_list에 넣지 않는다는 Pintos의 규칙도 깨짐. (실제로 idle이 timer_sleep()을 부를 일은 없지만 ㅎㅎ)
  ASSERT(cur!= idle_thread);
  // 인터럽트를 끄고 이전 상태를 돌려받음
  // list에 넣는 도중 타이머 인터럽트의 thread_wake_up()이 같은 리스트를 건드리면 안 되므로,
  // 리스트에 넣은 뒤 thread_block() 전에 인터럽트가 꺠우려 하면 아직 RUNNING이라
  // thread_unblock()의 ASSERT(status==THREAD_BLOCKED)가 실패한다
  old_level = intr_disable();
  cur->wakeup_tick = wakeup_tick; // 깨워야 할 시각을 스레드에 기록.(리스트에 넣기 전에!)
  // list를 앞에서부터 보며 wakeup_less(새 노드, e)가 처음 true가 되는 e 앞에 삽이반다. 
  // -> 리스트는 깨어날 시각 오름 차순으로 정렬됨. 
  list_insert_ordered(&sleep_list, &cur->sleep_elem, wakeup_less, NULL);
  // 상태를 BLOCKED로 바꾸고 schedule()로 다른 스레드에게 CPU를 넘긴다. 
  // 이 스레드 실행은 여기서 멈추고, 나중에 누군가 thread_unblock()해서 다시 스케줄되면 바로 다음 줄부터 이어서 실행됨. 
  thread_block(); // 재우기
  intr_set_level(old_level); // 깨어난 뒤 인터럽트를 이전 상태로 되돌림. 
}

void
thread_wake_up(void)
{
  int64_t now = timer_ticks(); // 현재 틱(방금 ticks++된 값)
  while(!list_empty(&sleep_list)) // 잠든 스레드가 남아 있는 동안 반복
  {
    // 맨 앞 노드에 해당하는 스레드를 찾음.(깨우기 위해서)
    struct thread *t = list_entry(list_front(&sleep_list),struct thread, sleep_elem);
    if(t->wakeup_tick > now) // front도 아직 깨울 시각이 안 됐으면 멈춘다.(정렬해뒀기 때문)
      break;
    // front를 깨울 시각이 됐다면 해당 스레드를 sleep_list에서 pop한다.
    list_pop_front(&sleep_list);
    // 해당 스레드를 BLOCKED->READY로 바꾸고 ready_list에 넣는다.
    // 깨어난 스레드는 thread_sleep()의 thread_block() 다음 줄부터 다시 실행되면 선점하지 않는다. 
    thread_unblock(t);
  }
  // 루프가 끝난 후 한 번만 호출
  // 꺠운 스레드 중 지금 실행 중인 스레드보다 우선 순위가 높은 스레드가 있으면 양보한다. 
  // 방금 깨운 스레드가 지금 실행 중인 스레드보다 우선 순위가 높을 수 있음. 
  thread_check_preempt();
}

/* a의 우선순위가 b보다 높으면 true (내림차순 정렬 기준) */
bool
thread_priority_greater(const struct list_elem *a, const struct list_elem *b,
                        void *aux UNUSED)
{
  return list_entry(a, struct thread, elem)->priority
       > list_entry(b, struct thread, elem)->priority;
}

/* ready list에 현재 스레드보다 높은 우선순위가 있으면 CPU를 양보 */
void
thread_check_preempt(void)
{
  // 지금 CPU를 쓰고 있는 스레드.
  struct thread *cur = thread_current();
  // ready_list를 읽는 동안 타이머 인터럽트가 해당 리스트를 바꾸지 못하도록 인터럽트를 끈다.
  enum intr_level old_level = intr_disable();
  // 기다리는 스레드가 있어야 양보 가능. // 리스트의 front가 지금 스레드보다 우선순위가 > 일 때만 양보. 
  bool should_yield = !list_empty(&ready_list)
    && list_entry(list_front(&ready_list), struct thread, elem)->priority > cur->priority;
  intr_set_level(old_level);

  if(should_yield)
  {
    // 인터럽트 안에서 불린 경우(wake_up, aging)
    if(intr_context())
      intr_yield_on_return(); // 인터럽트 처리 중이면 끝난 뒤에 yield. (ASSERT(!intr_context())로 커널이 죽지 않도록)
    else
      thread_yield(); // 일반 실행 중이면 바로 양보. ㄴ
  }
}

#ifndef USERPROG
/* ready list에서 기다리는 스레드들의 우선순위를 1씩 올린다
   매 틱 thread_ticks()에서 호출되며 기다린 시간에 비례해 우선순위가 오른다. */
void
thread_aging(void)
{
  struct list_elem *e;
  enum intr_level old_level = intr_disable();

  // ready_list에 있는 스레드만 올림. (실행할 준비가 됐지만 순서가 안 오는 스레드)
  for(e = list_begin(&ready_list); e!= list_end(&ready_list); 
      e = list_next(e))
      {
        struct thread *t = list_entry(e, struct thread, elem);
        if(t->priority < PRI_MAX){
          t->priority++;
        }
      }

  // 2. 우선순위가 변경되었으므로 ready_list 전체를 우선순위 기준 내림차순으로 재정렬
  list_sort(&ready_list, thread_priority_greater, NULL);

  intr_set_level(old_level);
  thread_check_preempt(); // 올라간 스레드가 현재보다 높아졌다면 양보를 예약함. 
}
#endif


/* Puts the current thread to sleep.  It will not be scheduled
   again until awoken by thread_unblock().

   This function must be called with interrupts turned off.  It
   is usually a better idea to use one of the synchronization
   primitives in synch.h. */
void
thread_block (void) 
{
  ASSERT (!intr_context ());
  ASSERT (intr_get_level () == INTR_OFF);

  thread_current ()->status = THREAD_BLOCKED;
  schedule ();
}

/* Transitions a blocked thread T to the ready-to-run state.
   This is an error if T is not blocked.  (Use thread_yield() to
   make the running thread ready.)

   This function does not preempt the running thread.  This can
   be important: if the caller had disabled interrupts itself,
   it may expect that it can atomically unblock a thread and
   update other data. */
void
thread_unblock (struct thread *t) 
{
  enum intr_level old_level;

  ASSERT (is_thread (t));

  // ready_list는 타이머 인터럽트에서도 건드리므로 인터럽트를 끄고 조작.
  old_level = intr_disable ();
  ASSERT (t->status == THREAD_BLOCKED);
  // 우선 순위에 맞는 자리에 끼워넣음. 
  list_insert_ordered(&ready_list, &t->elem, thread_priority_greater, NULL);
  t->status = THREAD_READY; // 상태를 READY로 바꿈.
  intr_set_level (old_level); // 인터럽트 상태 복원. 
}

/* Returns the name of the running thread. */
const char *
thread_name (void) 
{
  return thread_current ()->name;
}

/* Returns the running thread.
   This is running_thread() plus a couple of sanity checks.
   See the big comment at the top of thread.h for details. */
struct thread *
thread_current (void) 
{
  struct thread *t = running_thread ();
  
  /* Make sure T is really a thread.
     If either of these assertions fire, then your thread may
     have overflowed its stack.  Each thread has less than 4 kB
     of stack, so a few big automatic arrays or moderate
     recursion can cause stack overflow. */
  ASSERT (is_thread (t));
  ASSERT (t->status == THREAD_RUNNING);

  return t;
}

/* Returns the running thread's tid. */
tid_t
thread_tid (void) 
{
  return thread_current ()->tid;
}

/* Deschedules the current thread and destroys it.  Never
   returns to the caller. */
void
thread_exit (void) 
{
  ASSERT (!intr_context ());

#ifdef USERPROG
  process_exit ();
#endif

  /* Remove thread from all threads list, set our status to dying,
     and schedule another process.  That process will destroy us
     when it calls thread_schedule_tail(). */
  intr_disable ();
  list_remove (&thread_current()->allelem);
  thread_current ()->status = THREAD_DYING;
  schedule ();
  NOT_REACHED ();
}

/* Yields the CPU.  The current thread is not put to sleep and
   may be scheduled again immediately at the scheduler's whim. */
void
thread_yield (void) 
{
  struct thread *cur = thread_current ();
  enum intr_level old_level;
  
  ASSERT (!intr_context ()); // 인터럽트 중에는 직접 양보 불가능. 

  old_level = intr_disable ();
  if (cur != idle_thread)  // idle 스레드는 ready_list에 넣지 않음. 아래는 자기 자신을 우선순위 자리에 넣는 코드. 
    list_insert_ordered(&ready_list, &cur->elem, thread_priority_greater, NULL);
  cur->status = THREAD_READY; // READY로 바꾸고
  schedule (); // 다음 스레드로 전환. 
  intr_set_level (old_level);
}

/* Invoke function 'func' on all threads, passing along 'aux'.
   This function must be called with interrupts off. */
void
thread_foreach (thread_action_func *func, void *aux)
{
  struct list_elem *e;

  ASSERT (intr_get_level () == INTR_OFF);

  for (e = list_begin (&all_list); e != list_end (&all_list);
       e = list_next (e))
    {
      struct thread *t = list_entry (e, struct thread, allelem);
      func (t, aux);
    }
}

/* Sets the current thread's priority to NEW_PRIORITY. */
void
thread_set_priority (int new_priority) 
{
  thread_current ()->priority = new_priority;
  // 우선 순위를 낮추게 되면 ready_list에 더 높은 게 있을 수 있음
  thread_check_preempt();
}

/* Returns the current thread's priority. */
int
thread_get_priority (void) 
{
  return thread_current ()->priority;
}

/* Sets the current thread's nice value to NICE. */
void
thread_set_nice (int nice UNUSED) 
{
  /* Not yet implemented. */
}

/* Returns the current thread's nice value. */
int
thread_get_nice (void) 
{
  /* Not yet implemented. */
  return 0;
}

/* Returns 100 times the system load average. */
int
thread_get_load_avg (void) 
{
  /* Not yet implemented. */
  return 0;
}

/* Returns 100 times the current thread's recent_cpu value. */
int
thread_get_recent_cpu (void) 
{
  /* Not yet implemented. */
  return 0;
}

/* Idle thread.  Executes when no other thread is ready to run.

   The idle thread is initially put on the ready list by
   thread_start().  It will be scheduled once initially, at which
   point it initializes idle_thread, "up"s the semaphore passed
   to it to enable thread_start() to continue, and immediately
   blocks.  After that, the idle thread never appears in the
   ready list.  It is returned by next_thread_to_run() as a
   special case when the ready list is empty. */
static void
idle (void *idle_started_ UNUSED) 
{
  struct semaphore *idle_started = idle_started_;
  idle_thread = thread_current ();
  sema_up (idle_started);

  for (;;) 
    {
      /* Let someone else run. */
      intr_disable ();
      thread_block ();

      /* Re-enable interrupts and wait for the next one.

         The `sti' instruction disables interrupts until the
         completion of the next instruction, so these two
         instructions are executed atomically.  This atomicity is
         important; otherwise, an interrupt could be handled
         between re-enabling interrupts and waiting for the next
         one to occur, wasting as much as one clock tick worth of
         time.

         See [IA32-v2a] "HLT", [IA32-v2b] "STI", and [IA32-v3a]
         7.11.1 "HLT Instruction". */
      asm volatile ("sti; hlt" : : : "memory");
    }
}

/* Function used as the basis for a kernel thread. */
static void
kernel_thread (thread_func *function, void *aux) 
{
  ASSERT (function != NULL);

  intr_enable ();       /* The scheduler runs with interrupts off. */
  function (aux);       /* Execute the thread function. */
  thread_exit ();       /* If function() returns, kill the thread. */
}

/* Returns the running thread. */
struct thread *
running_thread (void) 
{
  uint32_t *esp;

  /* Copy the CPU's stack pointer into `esp', and then round that
     down to the start of a page.  Because `struct thread' is
     always at the beginning of a page and the stack pointer is
     somewhere in the middle, this locates the curent thread. */
  asm ("mov %%esp, %0" : "=g" (esp));
  return pg_round_down (esp);
}

/* Returns true if T appears to point to a valid thread. */
static bool
is_thread (struct thread *t)
{
  return t != NULL && t->magic == THREAD_MAGIC;
}

/* Does basic initialization of T as a blocked thread named
   NAME. */
static void
init_thread (struct thread *t, const char *name, int priority)
{
  enum intr_level old_level;

  ASSERT (t != NULL);
  ASSERT (PRI_MIN <= priority && priority <= PRI_MAX);
  ASSERT (name != NULL);
  // 새로 할당된 struct thread 구조체의 전체 메모리 공간을 통쨰로 0으로 밀기.
  memset (t, 0, sizeof *t);
  t->status = THREAD_BLOCKED;
  strlcpy (t->name, name, sizeof t->name);
  t->stack = (uint8_t *) t + PGSIZE;
  t->priority = priority;
  t->magic = THREAD_MAGIC;

  /*
  // 세마포어 초기화 (첫 인자를 semaphore*로 받기 때문에 &t->sema를 넣어줘야 함.)
  sema_init(&t->wait_sema, 0);
  sema_init(&t->load_sema, 0);
  sema_init(&t->exit_sema, 0);
  */
  #ifdef USERPROG
    // 모든 스레드의 자식 리스트를 여기서 초기화. 
    list_init (&t->child_list);

  // memset으로 밀긴 하지만 명시적으로 한 번 더 적어줌(fd table의 경우는 memset을 사용해야 하니까 굳이 적지 않음)
  //t-> exit_status = 0;
  //t->load_success = false;

    t->parent = NULL;
    t->cp =NULL;
  #endif
  old_level = intr_disable ();
  list_push_back (&all_list, &t->allelem);
  intr_set_level (old_level);
}

/* Allocates a SIZE-byte frame at the top of thread T's stack and
   returns a pointer to the frame's base. */
static void *
alloc_frame (struct thread *t, size_t size) 
{
  /* Stack data is always allocated in word-size units. */
  ASSERT (is_thread (t));
  ASSERT (size % sizeof (uint32_t) == 0);

  t->stack -= size;
  return t->stack;
}

/* Chooses and returns the next thread to be scheduled.  Should
   return a thread from the run queue, unless the run queue is
   empty.  (If the running thread can continue running, then it
   will be in the run queue.)  If the run queue is empty, return
   idle_thread. */
static struct thread *
next_thread_to_run (void) 
{
  if (list_empty (&ready_list))
    return idle_thread;
  else
    return list_entry (list_pop_front (&ready_list), struct thread, elem);
}

/* Completes a thread switch by activating the new thread's page
   tables, and, if the previous thread is dying, destroying it.

   At this function's invocation, we just switched from thread
   PREV, the new thread is already running, and interrupts are
   still disabled.  This function is normally invoked by
   thread_schedule() as its final action before returning, but
   the first time a thread is scheduled it is called by
   switch_entry() (see switch.S).

   It's not safe to call printf() until the thread switch is
   complete.  In practice that means that printf()s should be
   added at the end of the function.

   After this function and its caller returns, the thread switch
   is complete. */
void
thread_schedule_tail (struct thread *prev)
{
  struct thread *cur = running_thread ();
  
  ASSERT (intr_get_level () == INTR_OFF);

  /* Mark us as running. */
  cur->status = THREAD_RUNNING;

  /* Start new time slice. */
  thread_ticks = 0;

#ifdef USERPROG
  /* Activate the new address space. */
  process_activate ();
#endif

  /* If the thread we switched from is dying, destroy its struct
     thread.  This must happen late so that thread_exit() doesn't
     pull out the rug under itself.  (We don't free
     initial_thread because its memory was not obtained via
     palloc().) */
  if (prev != NULL && prev->status == THREAD_DYING && prev != initial_thread) 
    {
      ASSERT (prev != cur);
      palloc_free_page (prev);
    }
}

/* Schedules a new process.  At entry, interrupts must be off and
   the running process's state must have been changed from
   running to some other state.  This function finds another
   thread to run and switches to it.

   It's not safe to call printf() until thread_schedule_tail()
   has completed. */
static void
schedule (void) 
{
  struct thread *cur = running_thread ();
  struct thread *next = next_thread_to_run ();
  struct thread *prev = NULL;

  ASSERT (intr_get_level () == INTR_OFF);
  ASSERT (cur->status != THREAD_RUNNING);
  ASSERT (is_thread (next));

  if (cur != next)
    prev = switch_threads (cur, next);
  thread_schedule_tail (prev);
}

/* Returns a tid to use for a new thread. */
static tid_t
allocate_tid (void) 
{
  static tid_t next_tid = 1;
  tid_t tid;

  lock_acquire (&tid_lock);
  tid = next_tid++;
  lock_release (&tid_lock);

  return tid;
}

/* Offset of `stack' member within `struct thread'.
   Used by switch.S, which can't figure it out on its own. */
uint32_t thread_stack_ofs = offsetof (struct thread, stack);
