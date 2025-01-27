
#include <assert.h>
#include <sys/mman.h>

#include "tinyos.h"
#include "kernel_cc.h"
#include "kernel_sched.h"
#include "kernel_proc.h"

#ifndef NVALGRIND
#include <valgrind/valgrind.h>
#endif


// Additions below - 2 lines. Declaration of a counter to increase on each yield's call. Definition of a limit after which re-prioritising happens
static int quantumCounter = 0;  //a counter to count the yield's,scheduling decisions that have been taken
#define MAXQ 100

/*
   The thread layout.
  --------------------

  On the x86 (Pentium) architecture, the stack grows upward. Therefore, we
  can allocate the TCB at the top of the memory block used as the stack.

  +-------------+
  |   TCB       |
  +-------------+
  |             |
  |    stack    |
  |             |
  |      ^      |
  |      |      |
  +-------------+
  | first frame |
  +-------------+

  Advantages: (a) unified memory area for stack and TCB (b) stack overrun will
  crash own thread, before it affects other threads (which may make debugging
  easier).

  Disadvantages: The stack cannot grow unless we move the whole TCB. Of course,
  we do not support stack growth anyway!
 */


/*
  A counter for active threads. By "active", we mean 'existing',
  with the exception of idle threads (they don't count).
 */
volatile unsigned int active_threads = 0;
Mutex active_threads_spinlock = MUTEX_INIT;

/* This is specific to Intel Pentium! */
#define SYSTEM_PAGE_SIZE  (1<<12)

/* The memory allocated for the TCB must be a multiple of SYSTEM_PAGE_SIZE */
#define THREAD_TCB_SIZE   (((sizeof(TCB)+SYSTEM_PAGE_SIZE-1)/SYSTEM_PAGE_SIZE)*SYSTEM_PAGE_SIZE)

#define THREAD_SIZE  (THREAD_TCB_SIZE+THREAD_STACK_SIZE)

//#define MMAPPED_THREAD_MEM
#ifdef MMAPPED_THREAD_MEM


/*
  Use mmap to allocate a thread. A more detailed implementation can allocate a
  "sentinel page", and change access to PROT_NONE, so that a stack overflow
  is detected as seg.fault.
 */
void free_thread(void* ptr, size_t size)
{
  CHECK(munmap(ptr, size));
}

void* allocate_thread(size_t size)
{
  void* ptr = mmap(NULL, size,
      PROT_READ|PROT_WRITE|PROT_EXEC,
      MAP_ANONYMOUS  | MAP_PRIVATE
      , -1,0);

  CHECK((ptr==MAP_FAILED)?-1:0);

  return ptr;
}
#else
/*
  Use malloc to allocate a thread. This is probably faster than  mmap, but cannot
  be made easily to 'detect' stack overflow.
 */
void free_thread(void* ptr, size_t size)
{
  //fprintf(stderr, "hrtha sthn free thread gia na eleutherwsw to %p",ptr);
  ptr = NULL ;
  free(ptr);
}

void* allocate_thread(size_t size)
{
  void* ptr = aligned_alloc(SYSTEM_PAGE_SIZE, size);
  CHECK((ptr==NULL)?-1:0);
  return ptr;
}
#endif

/*
  This is the function that is used to start normal threads.
*/

void gain(int preempt); /* forward */

static void thread_start()
{
  gain(1);
  CURTHREAD->thread_func();

  /* We are not supposed to get here! */
  assert(0);
}





/*
  Initialize and return a new TCB
*/


TCB* spawn_thread(PCB* pcb, void (*func)())
{
  /* The allocated thread size must be a multiple of page size */
  TCB* tcb = (TCB*) allocate_thread(THREAD_SIZE);

  /* Set the owner */
  tcb->owner_pcb=pcb;
  //tcb->owner_ptcb = ptcb ;
  /* Initialize the other attributes */
  tcb->type = NORMAL_THREAD;
  tcb->state = INIT;
  tcb->phase = CTX_CLEAN;
  ////
  tcb->priority = 0;
  tcb->mutexCount =0 ;
  tcb->prevPriority = 0 ;
  tcb->thread_func = func;
  tcb->wakeup_time = NO_TIMEOUT;
  rlnode_init(& tcb->sched_node, tcb);  /* Intrusive list node */

  tcb->its=QUANTUM;
  tcb->rts=QUANTUM;
  tcb->last_cause = SCHED_IDLE;
  tcb->curr_cause = SCHED_IDLE;

  /* Compute the stack segment address and size */
  void* sp = ((void*)tcb) + THREAD_TCB_SIZE;

  /* Init the context */
  cpu_initialize_context(& tcb->context, sp, THREAD_STACK_SIZE, thread_start);

#ifndef NVALGRIND
  tcb->valgrind_stack_id =
    VALGRIND_STACK_REGISTER(sp, sp+THREAD_STACK_SIZE);
#endif

  /* increase the count of active threads */
  Mutex_Lock(&active_threads_spinlock);
  active_threads++;
  Mutex_Unlock(&active_threads_spinlock);

  return tcb;
}


/*
  This is called with tcb->state_spinlock locked !
 */
void release_TCB(TCB* tcb)
{
#ifndef NVALGRIND
  VALGRIND_STACK_DEREGISTER(tcb->valgrind_stack_id);
#endif

  free_thread(tcb, THREAD_SIZE);

  Mutex_Lock(&active_threads_spinlock);
  active_threads--;
  Mutex_Unlock(&active_threads_spinlock);
}


/*
 *
 * Scheduler
 *
 */


/*
 *  Note: the scheduler routines are all in the non-preemptive domain.
 */


/* Core control blocks */
CCB cctx[MAX_CORES];


/*
  The scheduler queue is implemented as a doubly linked list. The
  head and tail of this list are stored in  SCHED.

  Also, the scheduler contains a linked list of all the sleeping
  threads with a timeout.

  Both of these structures are protected by @c sched_spinlock.
*/


rlnode SCHED[PRIORITY_LEVELS];                         /* The scheduler LIST OF queues */
rlnode TIMEOUT_LIST;				  /* The list of threads with a timeout */
Mutex sched_spinlock = MUTEX_INIT;    /* spinlock for scheduler queue */



/* Interrupt handler for ALARM */
void yield_handler()
{
  yield(SCHED_QUANTUM);
}

/* Interrupt handle for inter-core interrupts */
void ici_handler()
{
  /* noop for now... */
}


/*
  Possibly add TCB to the scheduler timeout list.

  *** MUST BE CALLED WITH sched_spinlock HELD ***
*/
static void sched_register_timeout(TCB* tcb, TimerDuration timeout)
{

  if(timeout!=NO_TIMEOUT){

  	/* set the wakeup time */
  	TimerDuration curtime = bios_clock();
  	tcb->wakeup_time = (timeout==NO_TIMEOUT) ? NO_TIMEOUT : curtime+timeout;

  	/* add to the TIMEOUT_LIST in sorted order */
  	rlnode* n = TIMEOUT_LIST.next;
  	for( ; n!=&TIMEOUT_LIST; n=n->next)
  		/* skip earlier entries */
  		if(tcb->wakeup_time < n->tcb->wakeup_time) break;
  	/* insert before n */
	rl_splice(n->prev, & tcb->sched_node);
  }
}


/*
  Add TCB to the end of the scheduler list.

  *** MUST BE CALLED WITH sched_spinlock HELD ***
*/
static void sched_queue_add(TCB* tcb)
{
  /* Insert at the end of the scheduling list */
  rlist_push_back(& SCHED[tcb->priority], & tcb->sched_node);

  /* Restart possibly halted cores */
  cpu_core_restart_one();
}


/*
	Adjust the state of a thread to make it READY.

    *** MUST BE CALLED WITH sched_spinlock HELD ***
 */
static void sched_make_ready(TCB* tcb)    /*pairnei to tcb to kanei ready kai to vazei sto telos tou sched*/
{
	assert(tcb->state == STOPPED || tcb->state == INIT);

	/* Possibly remove from TIMEOUT_LIST */
	if(tcb->wakeup_time != NO_TIMEOUT) {
		/* tcb is in TIMEOUT_LIST, fix it */
		assert(tcb->sched_node.next != &(tcb->sched_node) && tcb->state == STOPPED);
		rlist_remove(& tcb->sched_node);
		tcb->wakeup_time = NO_TIMEOUT;
	}

	/* Mark as ready */
	tcb->state = READY;

	/* Possibly add to the scheduler queue */
	if(tcb->phase == CTX_CLEAN)
		sched_queue_add(tcb);
}

/*
  Scan the \c TIMEOUT_LIST for threads whose timeout has expired, and
  wake them up.
  *** MUST BE CALLED WITH sched_spinlock HELD ***
*/

static void sched_wakeup_expired_timeouts()
{
  /* Empty the timeout list up to the current time and wake up each thread */
  TimerDuration curtime = bios_clock();

  while (!is_rlist_empty(&TIMEOUT_LIST)) {
    TCB* tcb = TIMEOUT_LIST.next->tcb;
    if (tcb->wakeup_time > curtime)
      break;
    sched_make_ready(tcb);
  }
}


/*
  Remove the head of the scheduler list, if any, and
  return it. Return NULL if the list is empty.

  *** MUST BE CALLED WITH sched_spinlock HELD ***
*/

static TCB* sched_queue_select(TCB *current,enum SCHED_CAUSE cause)
{
  

  
  
  // define an offset for computations  [Todo: change it to a permanent value later]
  int offset = 2; 

  
  switch(cause)
  {

    case SCHED_QUANTUM:   //quantum has expired

      if ((current->priority < (PRIORITY_LEVELS - offset )) && (current->mutexCount == 0))
      {
        //high priority thread, not denied mutex access so decreasing priority by adding 1 to it

        current->priority = current->priority + 1;   


      }
      else if ( current->mutexCount != 0)
      {

          current->mutexCount = 0; //restore mutexCounter
          current->priority = current->prevPriority; // get your previousPriority back  
      }   

    case SCHED_IO:
      
      

      if ((current->priority != 0) && (current->mutexCount == 0))
        {
          current->priority = current->priority - 1; //Increase priority order 
        }
      else if (current->mutexCount != 0)
      {
        current->mutexCount = 0; //restore mutexCounter
        current->priority = current->prevPriority; // get your previousPriority back  
      } 

    case SCHED_MUTEX:

      current->mutexCount=current->mutexCount + 1; //Increase the counter when thread's mutex access get a lock 
      if( current->mutexCount == 1)
      {
          current->prevPriority = current->priority; //store the priority the first time that a thread is denied mutex access


      }
      else if ( current->priority < PRIORITY_LEVELS - offset )
      {
        current->priority = current->priority + 1; //decrease priority
      }
    case SCHED_PIPE:  //Thread sleep at a pipe
    case SCHED_POLL:  //Thread is polling a device
    case SCHED_IDLE:  //Idle thread called yield,which called us ofc
    case SCHED_USER:  //User space code called yield, which did the same
    break;

    default:

    // If execution reaches here there has been a malfunction.
    fprintf(stderr, "BAD CAUSE for current thread %p in yield function %d\n",current,cause);
    assert(0);
  } 



  if (quantumCounter >= MAXQ)  //MAX_Q =100
  {
    rearrangePriorities();  // boost low - prioritised threads
    quantumCounter = 0; // reset qCounter
  }

  

  //Note: maybe refactor scheduler and call sched_wakeup_expired_timeouts() here!


  
  int actualLevel = 0;
  

  
  // Seek for the proper level and thread to run
  while(actualLevel < PRIORITY_LEVELS -1 && is_rlist_empty(& SCHED[actualLevel]))
  {
      actualLevel++;

  }

  /* Get the head of the SCHED list 
    Additions: head of the SCHED[level] list    
  */
  rlnode* sel = rlist_pop_front(& SCHED[actualLevel]);
  TCB* next_thread = sel->tcb; /* When the list is empty, this is NULL */

  //Should't get here after additions

  if (next_thread == NULL)
    next_thread = (current->state == READY) ? current : &CURCORE.idle_thread;

  next_thread->its = QUANTUM;

  return next_thread;
}



void rearrangePriorities()
{
  
  for (int i = (PRIORITY_LEVELS-1); i < 0; i--) 
  {
    //Iterate each level of scheduler_queue starting from low to high exluding top level of SCHED[i]
    while(! is_rlist_empty(&SCHED[i]))
    {
      TCB* aThread = SCHED[i].next->tcb;
      aThread->priority --; ///boost priority up to a higher level, assign 0 would also be a solution [Impose highest possible priority]
      rlnode *someNode;
      someNode = rlist_pop_front(& SCHED[i]);

      //Re-Enter with changed priority.
      sched_queue_add(someNode->tcb);

    }
  }
}




/*
  Make the process ready.
 */
int wakeup(TCB* tcb)
{
	int ret = 0;

	/* Preemption off */
	int oldpre = preempt_off;

	/* To touch tcb->state, we must get the spinlock. */
	Mutex_Lock(& sched_spinlock);

	if(tcb->state==STOPPED || tcb->state==INIT) {
		sched_make_ready(tcb);
		ret = 1;
	}

	Mutex_Unlock(& sched_spinlock);

	/* Restore preemption state */
	if(oldpre) preempt_on;

	return ret;
}


/*
  Atomically put the current process to sleep, after unlocking mx.
 */
void sleep_releasing(Thread_state state, Mutex* mx, enum SCHED_CAUSE cause, TimerDuration timeout)
{
  assert(state==STOPPED || state==EXITED);
  TCB* tcb = CURTHREAD;
  /*
    The tcb->state_spinlock guarantees atomic sleep-and-release.
    But, to access it safely, we need to go into the non-preemptive
    domain.
   */
  int preempt = preempt_off;
  Mutex_Lock(& sched_spinlock);

  /* mark the thread as stopped or exited */
  tcb->state = state;

  /* register the timeout (if any) for the sleeping thread */
  if(state!=EXITED)
  	sched_register_timeout(tcb, timeout);

  /* Release mx */
  if(mx!=NULL) Mutex_Unlock(mx);

  /* Release the schduler spinlock before calling yield() !!! */
  Mutex_Unlock(& sched_spinlock);

  /* call this to schedule someone else */
  yield(cause);

  /* Restore preemption state */
  if(preempt) preempt_on;
}


/* This function is the entry point to the scheduler's context switching */

void yield(enum SCHED_CAUSE cause)
{
  /* Reset the timer, so that we are not interrupted by ALARM */

  TimerDuration remaining = bios_cancel_timer();
  quantumCounter++;
  /* We must stop preemption but save it! */
  int preempt = preempt_off;

  TCB* current = CURTHREAD;  /* Make a local copy of current process, for speed */

  

  Mutex_Lock(& sched_spinlock);
 

  /* Update CURTHREAD state */
  if (current->state == RUNNING)
    current->state = READY;
  

  /* Update CURTHREAD scheduler data */
  current->rts = remaining;
  current->last_cause = current->curr_cause;
  current->curr_cause = cause;


  /* Wake up threads whose sleep timeout has expired */

  sched_wakeup_expired_timeouts();

  /* Get next */
  TCB* next = sched_queue_select(current, cause);
 

  /* Maybe there was nothing ready in the scheduler queue ? */
  if(next==NULL) 
  {
    if(current->state == READY)
      next = &CURCORE.current_thread;
    else
      next = &CURCORE.idle_thread;
  }
  

  /*link the current and next TCB, for the gain phase */
  current->next = next;
  next->prev = current;

  if(next != NULL)
  {
      CURCORE.previous_thread = current;
  }
 

  Mutex_Unlock(& sched_spinlock);

  /* Switch contexts */
  if(current!=next) {
      
      CURTHREAD = next;

      cpu_swap_context( & current->context , & next->context );
  }
  /* This is where we get after we are switched back on! A long time
     may have passed. Start a new timeslice...
   */
  gain(preempt);
}


/*
  This function must be called at the beginning of each new timeslice.
  This is done mostly from inside yield().
  However, for threads that are executed for the first time, this
  has to happen in thread_start.

  The 'preempt' argument determines whether preemption is turned on
  in the new timeslice. When returning to threads in the non-preemptive
  domain (e.g., waiting at some driver), we need to not turn preemption
  on!
*/

void gain(int preempt)
{
  Mutex_Lock(& sched_spinlock);
  /* Mark current state */
  TCB* current = CURTHREAD;
  TCB* prev = current->prev;
  current->rts = current->its;

  current->state = RUNNING;
  current->phase = CTX_DIRTY;

  if(current != prev) {
  	/* Take care of the previous thread */
    prev->phase = CTX_CLEAN;
    switch(prev->state)
    {
      case READY:
        if(prev->type != IDLE_THREAD) sched_queue_add(prev);
        break;
      case EXITED:
        release_TCB(prev);
        break;
      case STOPPED:
        break;
      default:
        assert(0);  /* prev->state should not be INIT or RUNNING ! */
    }
  }
  

  Mutex_Unlock(& sched_spinlock);

  /* Reset preemption as needed */
  if(preempt) 
    preempt_on;

  /* Set an 1-quantum alarm */
  bios_set_timer(current->rts);

}


static void idle_thread()
{
  /* When we first start the idle thread */
  yield(SCHED_IDLE);

  /* We come here whenever we cannot find a ready thread for our core */
  while(active_threads>0) {
    cpu_core_halt();
    yield(SCHED_IDLE);
  }

  /* If the idle thread exits here, we are leaving the scheduler! */
  bios_cancel_timer();
  cpu_core_restart_all();
}


/*
  Initialize the scheduler queue
 */
void initialize_scheduler()
{
  int i ;
  for(i=0;i <PRIORITY_LEVELS ;i++)
  {
      rlnode_init(&SCHED[i], NULL);
  }
  rlnode_init(&TIMEOUT_LIST, NULL);
}


void run_scheduler()
{
  CCB * curcore = & CURCORE;

  /* Initialize current CCB */
  curcore->id = cpu_core_id;

  curcore->current_thread = & curcore->idle_thread;

  curcore->idle_thread.owner_pcb = get_pcb(0);
  curcore->idle_thread.type = IDLE_THREAD;
  curcore->idle_thread.state = RUNNING;
  curcore->idle_thread.phase = CTX_DIRTY;
  curcore->idle_thread.wakeup_time = NO_TIMEOUT;
  rlnode_init(& curcore->idle_thread.sched_node, & curcore->idle_thread);

  curcore->idle_thread.its = QUANTUM;
  curcore->idle_thread.rts = QUANTUM;

  curcore->idle_thread.curr_cause = SCHED_IDLE;
  curcore->idle_thread.last_cause = SCHED_IDLE;


  /* Initialize interrupt handler */
  cpu_interrupt_handler(ALARM, yield_handler);
  cpu_interrupt_handler(ICI, ici_handler);

  /* Run idle thread */
  preempt_on;
  idle_thread();

  /* Finished scheduling */
  assert(CURTHREAD == &CURCORE.idle_thread);
  cpu_interrupt_handler(ALARM, NULL);
  cpu_interrupt_handler(ICI, NULL);
}