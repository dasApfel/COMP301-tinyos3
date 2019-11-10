
#include "tinyos.h"
#include "kernel_sched.h"
#include "kernel_proc.h"


/*

@brief Create a new thread()

maybe not needed,we'll see..
*/

void start_new_thread()
{
  int exitValue;
  PTCB somePTCB = (PTCB *)sys_ThreadSelf();
  Task call = somePTCB->main_task;
  int argl = somePTCB->argl;
  void* args = somePTCB->args;
  exitValue = call(argl,args);
  ThreadExit(exitValue);





}


/* 
  @brief Create a new thread in the current process.Similar to sys_Exec
  This function is provided as an argument to spawn thread to create a new TCB linked to this PTCB (1-1 connection).
  */
Tid_t sys_CreateThread(Task task, int argl, void* args)
{


  CURPROC->threadCount++;

  //Initialise member variables.

  PTCB *aptcb = malloc(sizeof(PTCB)); //reference pointer, dynamically alloc. space
 
  aptcb->main_task = call;
  aptcb->argl = argl;
  aptcb->exited=0;
  aptcb->isDetached=0;
  aptcb->isExistant=0;
  aptcb->refCounter=0;
  aptcb->cVar = COND_INIT;
  aptcb->isExited=0;

	 if (args == NULL) {
        ptcb->args = NULL;
    }
    else
    {
        ptcb->args = args;
    }

    rlnode *ptcbNode = rlnode_init(&aptcbNode->aNode, aptcb);
    rlist_push_back(&CURPROC->thread_list, ptcbNode);

    if (task != NULL) {
        ptcb->thread = spawn_thread(CURPROC,start_new_thread);  
        wakeup(ptcb->thread);   //make a tcb ready to be imported on scheduler
    }

    return (Tid_t) aptcb->thread;
}

/**
  @brief Return the Tid of the current thread.

  Modified: 9/11/2019

  -get the current process's thread list (PCB->thread_list)  =>typeof(thread_list) == (PTCB *)rlnode
  -itterate every thread connected with the CURPROC
  -mark the position of CURTHREAD
  -return T_Id of PTCB showing to that.
 */
Tid_t sys_ThreadSelf()
{

  PTCB* currPTCB=NULL;
  int ptcbLen=rlist_len(&CURPROC->thread_list)  //get the length of the threads list in the current process
  int counter = 0;
  int found = 0;  //flag if something is found => (found = 1)
  rlnode *aNode= (&CURPROC->thread_list)->next;
  //itterate as long the index is fine, or nothing is found (if found then stop)
  while(counter < ptcbLen || found == 0)
  {

    //if the current thread is stored here  
    if ( aNode->ptcb->thread ==(TCB *) CURTHREAD )
    {
      currPTCB = aNode->ptcb;  //copy it
      found = 1; //flag sucess in finding process
    }

    //else,move to the next node
    aNode = aNode->next;
    counter++;
  }



	return (Tid_t) currPTCB;
}

/**
  @brief Join the given thread.
  
  -  

  */
int sys_ThreadJoin(Tid_t tid, int* exitval)
{
	
}

/**
  @brief Detach the given thread.
  */
int sys_ThreadDetach(Tid_t tid)
{
	return -1;
}

/**
  @brief Terminate the current thread.
  */
void sys_ThreadExit(int exitval)
{

return -1;

}

