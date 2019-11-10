
#include "tinyos.h"
#include "kernel_sched.h"
#include "kernel_proc.h"


/*

@brief Create a new thread()

Definetlly needed nigga..
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
  aptcb->isDetached=0;
  aptcb->refCounter=0;
  aptcb->cVar = COND_INIT;
  aptcb->hasExited=0;

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
	return -1;
}

/**
  @brief Detach the given thread. Detachable: a thread that cannot be joined by any other.."Mum, Im a grown up now"
  */
int sys_ThreadDetach(Tid_t tid)
{
	
  PTCB* somePTCB=NULL;
  
  int ret=0;
  int len=rlist_len(&CURPROC->thread_list);
  int counter=0;

  rlnode *node=(&CURPROC->thread_list)->next;


  // itterate the whole thread_list of the current process
  while(counter < len)
  {

    //if the current_node of the thread_list has a thread whicha has t_id == given_t_id,mark it
    if(node->ptcb->thread == (TCB *) tid)
    {

        somePTCB = node->ptcb;

    }

    node = node->next;
    counter++


    if(somePTCB == NULL || somePTCB->hasExited != 0)
    {
      //Unable to detach a NULL thread or a ZOMBIE thread
      ret = -1;
    }
    else
    {

      ((PTCB *)tid)->isDetached = 1; //flag the detachment
      kernel_broadcast(&somePTCB->cVar);

    }

  }

  return ret;
}

/**
  @brief Terminate the current thread.



  
  */
void sys_ThreadExit(int exitval)
{

  //locate PTCB linked to the current thread

  PTCB *currPTCB= (PTCB*)sys_ThreadSelf();
  
  CURPROC->threadCount--; //thread counter should be adjusted accordingly
  currPTCB->hasExited = 1; //we're about to exit,so mark that fact please dear function


/* if curr_thread != main_thread we should adjust the exitVal to the given one
   if not there is no need to worry, main_thread is covered in kernel_proc.c so we're fine..i guess
*/

  if(CURPROC->exitFlag == 0) 
  {
    currPTCB->exitVal = exitval;
  }
  //broadcast condVariables to kernel
  kernel_broadcast(&currPTCB->cVar);
  kernel_broadcast(&CURPROC->aCond);

  //Check if current thread is the last one, inside a list linked with the current procedure (PCB)
  if(CURPROC->threadCount == 0)
  {

      PCB *curproc=CURPROC // make a local copy - cache for higher efficiency level

      //Clean up whatever sysExit would clean but also for a "regular" thread as well.
      //Lines cropped and adjusted from sysExit (kernel_proc.c), no need for them to be there any more

      /* Do all the other cleanup we want here, close files etc. */
      
      if(curproc->args) 
      {
        free(curproc->args);  //clean the argument stream,pleeease sir
        curproc->args = NULL;
      }

        /* Clean up FIDT */
      for(int i=0;i<MAX_FILEID;i++) 
      {
        if(curproc->FIDT[i] != NULL) 
        {
          FCB_decref(curproc->FIDT[i]);
          curproc->FIDT[i] = NULL;
        }
      }

       /* Reparent any children of the exiting process to the 
     initial task */
      PCB* initpcb = get_pcb(1);
      while(!is_rlist_empty(& curproc->children_list)) 
      {
        rlnode* child = rlist_pop_front(& curproc->children_list);
        child->pcb->parent = initpcb;
        rlist_push_front(& initpcb->children_list, child);
      }

      /* Add exited children to the initial task's exited list 
         and signal the initial task */
      if(!is_rlist_empty(& curproc->exited_list)) 
      {
        rlist_append(& initpcb->exited_list, &curproc->exited_list);
        kernel_broadcast(& initpcb->child_exit);
      }

      /* Put me into my parent's exited list */
      if(curproc->parent != NULL)
       {   /* Maybe this is init */
        rlist_push_front(& curproc->parent->exited_list, &curproc->exited_node);
        kernel_broadcast(& curproc->parent->child_exit);
      }

      //whole process is now exited, or more like a ZOMBIE (@vsam said that zombie thing, not me)

      curproc->pstate = ZOMBIE;
  }
 //goodbye my lover, goodbye my friend..
 kernel_sleep(EXITED,SCHED_USER) 
}

