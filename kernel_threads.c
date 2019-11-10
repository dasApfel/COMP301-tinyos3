
#include "tinyos.h"
#include "kernel_sched.h"
#include "kernel_proc.h"

/** 
  @brief Create a new thread in the current process.
  */
Tid_t sys_CreateThread(Task task, int argl, void* args)
{
	return NOTHREAD;
}

/**
  @brief Return the Tid of the current thread.

  Modified: 9/11/2019

  -get the current process's thread list (PCB->thread_list)  =>typeof(thread_list) == (PTCB *)rlnode
  -itterate every thread connected with the CURPROC
  -mark the position of CURTHREAD
  -return T_Id of CURTHREAD
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



	return (Tid_t) CURTHREAD;
}

/**
  @brief Join the given thread.
  */
int sys_ThreadJoin(Tid_t tid, int* exitval)
{
	return -1;
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

}

