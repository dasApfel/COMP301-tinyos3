
#include "tinyos.h"
#include "kernel_cc.h"
#include "kernel_streams.h"



// Define operations in sockets

static file_ops socketOps = {

  .Open = NULL,
  .Read = socket_read,
  .Write = socket_write,
  .Close = socket_close




};

SCB *port_map[MAX_PORT + 1] = {NULL};




/*

  Socket Actions Implementation 


*/




int socket_read(void *givenSCB, char *buf, unsigned int size) 
{


  
    //caching to impose efficiency
    SCB *scb = (SCB *) givenSCB;
    

    //peers read data, others not
    if (scb->socketType == PEER) 
    {

        // setup a pipe 
        PipeCB *pipeCB = scb->CBT.peer_ops->server;

        return pipe_read(pipeCB, buf, size);

      
    }
            
   //return failure 
    return -1;
}

int socket_write(void *givenSCB, const char *buf, unsigned int size) 
{
  
    //caching to impose speed
    SCB *scb = (SCB *) givenSCB;
  

    //same is also applicable on writing
    if (scb->socketType == PEER) 
    {
       PipeCB *pipeCB = scb->CBT.peer_ops->client; 
       return pipe_write(pipeCB, buf, size);
    }

   //return failure
        
    
    return -1;
}

int socket_close(void *givenSCB)
{

  //caching and checking
  SCB *scb = (SCB *)givenSCB;


  if(scb == NULL)
  {

    return -1;

  }
  switch(scb->socketType)
  {

    case UNBOUND:
      break;
    case LISTENER:

      // wipe out the port
      port_map[scb->boundPort] = NULL;
            kernel_signal(&scb->CBT.listener_ops->cv);

       while (!is_rlist_empty(&scb->CBT.listener_ops->buffer_queue)) 
      {
                  rlnode *reqNode = rlist_pop_front(&scb->CBT.listener_ops->buffer_queue);
                  kernel_signal(&reqNode->request->cv);
            }

            free(scb->CBT.listener_ops);
            break;



    case PEER:


      if (scb->CBT.peer_ops->buddy!=NULL)
       {
                pipe_close_reader(scb->CBT.peer_ops->server);
                pipe_close_writer(scb->CBT.peer_ops->client);
                scb->CBT.peer_ops->buddy->CBT.peer_ops->buddy = NULL;
            }
            free(scb->CBT.peer_ops);
            break;





  }


  free(scb);
  return 0;

}

Fid_t sys_Socket(port_t port)
{



  // 1 FCB and 1 F_id needed for the op
    Fid_t fid;
    FCB *fcb;
    
  // illegal ports

  if (port < 0 || port > MAX_PORT)
  {
         return NOFILE;
    }

   
   
    // Unlike scenario that the reservation of FCB's never made

     if (FCB_reserve(1, &fid, &fcb) == 0)
     {
         return NOFILE;
     }

      // initialization

     SCB *scb = (SCB *) xmalloc(sizeof(SCB));
     scb->fid = fid;
     scb->fcb = fcb;

     //connect - bound the socket to the specific port
     scb->boundPort = port;
     scb->socketType = UNBOUND;
     //streamobj -> socket_control block , streamfunc -> socketOps 
     fcb->streamobj = scb;
     fcb->streamfunc = &socketOps;

//return a file id for the newly made socket
  return fid;
}

int sys_Listen(Fid_t sock)
{

  //get the streamobj in terms of SCB, cache it for efficiency
  SCB *scb = find_scb(sock);

 
  //We don't like unbound sockets for this one,sorry!
  /*
    UNBOUND socket: LISTENER or PEER so not suitable




  */  

    if (scb == NULL || scb->socketType != UNBOUND || scb->boundPort == 0 ||port_map[scb->boundPort] != NULL) 
    {
        return -1;
    }

    //update socket's status and establish proper connections - transform to a listener

    scb->socketType = LISTENER;
    scb->CBT.listener_ops = (Listener *) xmalloc(sizeof(Listener));
    scb->CBT.listener_ops->cv = COND_INIT;
    rlnode_new(&scb->CBT.listener_ops->buffer_queue);
    port_map[scb->boundPort] = scb;


  return 0; 
}




int sys_Connect(Fid_t sock, port_t port, timeout_t timeout)
{

  // obtain the socket trying to connect
  SCB *scb = find_scb(sock);

  //check the legality 
    if (port < 0 || port >= MAX_PORT || port_map[port] == NULL ||
       port_map[port]->socketType != LISTENER || scb->socketType != UNBOUND || scb==NULL ) 
    {
        return -1;
    }


    //allocate requests space and init the control block
    Request *request = (Request *)xmalloc(sizeof(Request));
    request->cv = COND_INIT;
    request->admitted = 0;
    request->scb = find_scb(sock);

    rlnode insertnode;
    rlnode_init(&insertnode, request);

    rlist_push_back(&port_map[port]->CBT.listener_ops->buffer_queue, &insertnode);

    //wakeup the listener, to serve the new request. THIS MUST BE SERVED FROM LISTENER SOCKETS
    kernel_signal(&port_map[port]->CBT.listener_ops->cv);

    // returns 1 if successful or 0 f timed out
    kernel_timedwait(&request->cv, SCHED_USER, timeout);
 
    rlist_remove(&insertnode);
 


    return (request->admitted -1);
  
}

Fid_t sys_Accept(Fid_t lsock)
{

  //locate the given socket in terms of SCB, the cache it
  SCB *listenerSCB = find_scb(lsock);


  // Unlike scenario that acceptance cannot be made
    if (listenerSCB == NULL || listenerSCB->socketType != LISTENER ) 
    {

     
      return NOFILE;
    }


    // block everything until somebody comes in with a request

    while (is_rlist_empty(&listenerSCB->CBT.listener_ops->buffer_queue)) 
    {
        kernel_wait(&listenerSCB->CBT.listener_ops->cv, SCHED_USER);
    }


    // cache the existent data and the request as well
    rlnode *requestNode = rlist_pop_front(&listenerSCB->CBT.listener_ops->buffer_queue);
    Request *request = requestNode->request;

   
    if (find_scb(lsock) == NULL) 
    {
      kernel_signal(&request->cv);
      return NOFILE;

    }

    // P2P Connection: Create a new socket Unbound in SAME port.

    Fid_t peer1fid = sys_Socket(listenerSCB->boundPort);

    if (peer1fid == NOFILE) 
    {
      kernel_signal(&request->cv);
      return NOFILE;
    }



    //Both sockets should become peers

    SCB *peer1 = find_scb(peer1fid);
    SCB *peer2 = request->scb;
    peer1->CBT.peer_ops = (Peer *) xmalloc(sizeof(Peer));
    peer2->CBT.peer_ops = (Peer *) xmalloc(sizeof(Peer));

    pipe_t pipe1, pipe2;
    Fid_t fid[2];
    
    FCB *fcb[2];
    fid[0] = request->scb->fid;
    fid[1] = peer1fid;
    fcb[0] = request->scb->fcb;
    fcb[1] = get_fcb(peer1fid);
    PipeCB *pipeCB1 = Pipe_Init(&pipe1, fid, fcb);
    fid[0] = peer1fid;
    fid[1] = request->scb->fid;
    fcb[0] = get_fcb(peer1fid);
    fcb[1] = request->scb->fcb;

    //initialization and p2p connection 1's buddy is 2 and vice versa.

    PipeCB *pipeCB2 = Pipe_Init(&pipe2, fid, fcb);
    peer1->CBT.peer_ops->client = pipeCB1;
    peer1->CBT.peer_ops->server = pipeCB2;
    peer1->CBT.peer_ops->buddy = peer2;
    peer1->socketType = PEER;
    peer2->CBT.peer_ops->client = pipeCB2;
    peer2->CBT.peer_ops->server = pipeCB1;
    peer2->CBT.peer_ops->buddy = peer1;
    peer2->socketType = PEER;
    request->admitted = 1;

    //caller socket
    kernel_signal(&request->cv);

    return peer1fid;
}



int sys_ShutDown(Fid_t sock, shutdown_mode manner)
{
  int retVal = -1;
    SCB *scb = find_scb(sock);
    if (scb==NULL) 
      return retVal;
    
    switch (manner) {
        case SHUTDOWN_READ:
            retVal = pipe_close_reader(scb->CBT.peer_ops->server);
            break;
        case SHUTDOWN_WRITE:
            retVal = pipe_close_writer(scb->CBT.peer_ops->client);
            break;
        case SHUTDOWN_BOTH:
            retVal = socket_close(scb);
            break;
    }
    return retVal;
}

