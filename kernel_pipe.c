
#include "tinyos.h"
#include "kernel_streams.h"
#include "kernel_cc.h"



/*

Define the operation set 

*/

int pipe_read(void *givenCB, char *buf, unsigned int length);
int pipe_write(void *givenCB, const char *buf, unsigned int size);
int pipe_close_reader(void *givenCB); 
int pipe_close_writer(void *givenCB);
int illegal_read(void *givenCB, char *buf, unsigned int s);
int illegal_write(void *givenCB,const char *buf, unsigned int s);

static file_ops readerOps = {

	.Open = NULL,
	.Read = pipe_read,
	.Write = illegal_write,
	.Close = pipe_close_reader


};

static file_ops writerOps = {

	.Open = NULL,
	.Read = illegal_read,
	.Write = pipe_write,
	.Close = pipe_close_writer


};








/*

	Initialize a Pipe Control Block and set up proper connections


*/

PipeCB *Pipe_Init(pipe_t *pipe , Fid_t *fid, FCB **fcb)
{


	// read and write end of a fid[2] array
	pipe->read = fid[0];
	pipe->write = fid[1];

	PipeCB *pipecb = (PipeCB *)xmalloc(sizeof(PipeCB));
	pipecb->pipe = pipe;
	pipecb->reader = fcb[0];
	pipecb->writer = fcb[1]; 
	pipecb->readerOff = 0;
	pipecb->writerOff = 0;
	pipecb->readPos = 0;
	pipecb->writePos = 0;

	pipecb->cv_read = COND_INIT;
	pipecb->cv_write = COND_INIT;




	return pipecb;




}

int sys_Pipe(pipe_t* pipe)
{
	Fid_t fid[2];
	FCB* fcb[2];
	
	

	//reserve FCBs - "I'd like a reservation for two please"

	// just testing the unlike scenario of failure in reservation => should be either 1 (success) or 0 (failure)
	if(!FCB_reserve(2,fid,fcb))
	{

		return -1;
	}

	// initialise a PipeCB to handle the pipping process

	PipeCB *p= Pipe_Init(pipe , fid, fcb);

	// connect both processes with the same Pipe Control Block

	fcb[0]->streamobj = p;
	fcb[1]->streamobj = p;


	// initialize functions for each process (read or write proc)

	fcb[0]->streamfunc = &readerOps;
	fcb[1]->streamfunc = &writerOps;

	// if -1 marks failure then in case of succesful completion we return 0.



	/* DUMB COMMENT JUST TO EXPRESS MY INFATUATION ABOUT YOU


	I'd like to announce that i find u a really interesting and beautiful person 
	In a manner that makes me feel that I want to spend more and more time with you!
	

	Who said that beeing in love will deprive my feelings about coding?

	*/

	return 0; 




}

int pipe_read(void *givenCB, char *buf, unsigned int length)
{

	unsigned int pos = 0;
	PipeCB *p = (PipeCB *)givenCB;

	if(p->readerOff == 1)
		return -1;

	//done deal, just get out
	if ((p->writerOff == 1) && (p->readPos == p->writePos))
		return 0;	

	for(pos = 0; pos< length; pos++,p->readPos = (p->readPos+1) % BUFFER_SIZE)
	{
		while (( p->writePos == p->readPos ) && (!p->writerOff))
		{
			kernel_broadcast(&p->cv_write);
			kernel_wait(&p->cv_read, SCHED_PIPE);
		}
		if(p->writePos == p->readPos && p->writerOff)
		{
			return pos;
		}
		buf[pos] = p->buffer[p->readPos];

	}

	kernel_broadcast(&p->cv_write);
	return pos;



}


int pipe_write(void *givenCB, const char *buf, unsigned int size) {

	unsigned int pos;
    PipeCB *p = (PipeCB *) givenCB;

    if (p->writerOff==1 || p->readerOff==1) 
    {
        return -1;
    }
    
    for (pos = 0; pos < size; pos++ ,p->writePos = (p->writePos + 1) % BUFFER_SIZE)
    {
        while ((p->writePos + 1) % BUFFER_SIZE == p->readPos && !p->readerOff) 
        {
            kernel_broadcast(&p->cv_read);
            kernel_wait(&p->cv_write, SCHED_PIPE);
        }
        if (p->writerOff==1 || p->readerOff==1) 
        {
            return -1;
        }
        p->buffer[p->writePos] = buf[pos];
        
    }
    kernel_broadcast(&p->cv_read);
    return pos;
}

int pipe_close_reader(void *givenCB) 
{
    PipeCB *p = (PipeCB *) givenCB;

    p->readerOff = 1;

    if (p->writerOff == 0)
    {
       kernel_broadcast(&p->cv_write);
    }
    else 
    {
      free (p);
    }

    return 0;
}

int pipe_close_writer(void *givenCB) 
{
    PipeCB *p = (PipeCB *) givenCB;

    p->writerOff = 1;

    if (p->readerOff == 0)
    {
       kernel_broadcast(&p->cv_read);
    }
    else 
    {
      free (p);
    }

    return 0;
}

// Case that a writer tries to access the reading end of a pipe.

int illegal_read(void *givenCB, char *buf, unsigned int s)
{
	return -1;
}

// Case that a reader tries to access the writing end of a pipe.

int illegal_write(void *givenCB,const char *buf, unsigned int s)
{

	return -1;
}


