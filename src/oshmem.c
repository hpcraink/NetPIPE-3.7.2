/* NOTE: Anywhere a sched_yield() is called, previously there was a busy
 * polling wait on the byte or flag, which caused horrible performance on the
 * machine I tested on (helix).  sched_yield() seemed to fix this issue. 
 * 
 * OpenSHMEM's API defines shmem_int_wait for exactly this reason.
 */

#include "netpipe.h"

double *pTime;
int    *pNrepeat;

void Init(ArgStruct *p, int* pargc, char*** pargv)
{
   shmem_init();
}

void Setup(ArgStruct *p)
{
   int npes;

   if((npes=shmem_n_pes())!=2) {

      fprintf(stderr,"Error Message: Run with npes set to 2\n");
      exit(1);
   }

   p->prot.flag=(int *) shmalloc(sizeof(int));
   pTime = (double *) shmalloc(sizeof(double));
   pNrepeat = (int *) shmalloc(sizeof(int));

   p->tr = p->rcv = 0;

   if((p->prot.ipe = shmem_my_pe()) == 0) {
      p->tr=1;
      p->prot.nbor=1;
      *p->prot.flag=1;

   } else {

      p->rcv=1;
      p->prot.nbor=0;
      *p->prot.flag=0;
   }
}

void Sync(ArgStruct *p)
{
   shmem_barrier_all();
}

void PrepareToReceive(ArgStruct *p) { }

void SendData(ArgStruct *p)
{
   if(p->bufflen%8==0)
      shmem_put64(p->s_ptr,p->r_ptr,p->bufflen/8,p->prot.nbor);
   else if(p->bufflen%4==0)
      shmem_put32(p->s_ptr,p->r_ptr,p->bufflen/4,p->prot.nbor);
   else if(p->bufflen%2==0)
      shmem_put16(p->s_ptr,p->r_ptr,p->bufflen/2,p->prot.nbor);
   else
      shmem_putmem(p->s_ptr,p->r_ptr,p->bufflen,p->prot.nbor);
   
   /* OpenSHMEM defines that data may be sent out-of-order.
    * Therefore plain busy-waiting on an expected valued at the
    * very end of the transmitted buffer, will not work.
    *
    * So, we NEED to signal via a specific flag.
    */
   shmem_int_p((int*)p->prot.flag, *p->prot.flag, p->prot.nbor);
}

void RecvData(ArgStruct *p)
{
   // Data sent is not initialized per default -- only with integCheck
   // Therefore, we need to shmem_int_wait for flag -- which !
#ifdef SHMEM_WAIT
   shmem_int_wait(p->prot.flag, p->prot.nbor);
#else
   int i=0;
   while(*p->prot.flag!=p->prot.ipe)
   { 
#  ifdef SCHED_YIELD
     sched_yield();
#  else
#    ifdef SCHED_DEBUG
     if(++i%100000000==0) { printf("d"); fflush(stdout); }
#    else
     if(++i%100000000==0) { printf(""); fflush(stdout); }
#    endif
#  endif
   }
#endif

   *p->prot.flag=p->prot.nbor;
}

void SendTime(ArgStruct *p, double *t)
{
   *pTime=*t;

   shmem_double_p(pTime,*pTime,p->prot.nbor);
   shmem_int_p((int*) p->prot.flag, *p->prot.flag,p->prot.nbor);
}

void RecvTime(ArgStruct *p, double *t)
{
#ifdef SHMEM_WAIT
   shmem_int_wait(p->prot.flag, p->prot.nbor);
#else
   int i=0;
   while(*p->prot.flag!=p->prot.ipe)
   { 
#  ifdef SCHED_YIELD
     sched_yield();
#  else
#    ifdef SCHED_DEBUG
     if(++i%100000000==0) { printf("t"); fflush(stdout); }
#    else
     if(++i%100000000==0) { printf(""); fflush(stdout); }
#    endif
#  endif
   }
#endif
   *t=*pTime; 
   *p->prot.flag=p->prot.nbor;
}

void SendRepeat(ArgStruct *p, int rpt)
{
   *pNrepeat= rpt;

   shmem_int_p(pNrepeat,*pNrepeat,p->prot.nbor);
   shmem_int_p((int*)p->prot.flag, *p->prot.flag,p->prot.nbor);
}

void RecvRepeat(ArgStruct *p, int *rpt)
{
#ifdef SHMEM_WAIT
   shmem_int_wait(p->prot.flag,p->prot.nbor);
#else
   int i=0;
   while(*p->prot.flag!=p->prot.ipe)
   {
#  ifdef SCHED_YIELD
     sched_yield();
#  else
#    ifdef SCHED_DEBUG
     if(++i%100000000==0) { printf("r"); fflush(stdout); }
#    else
     if(++i%100000000==0) { printf(""); fflush(stdout); }
#    endif
#  endif
   }
#endif
   *rpt=*pNrepeat;
   *p->prot.flag=p->prot.nbor;
}

void  CleanUp(ArgStruct *p)
{
}


void Reset(ArgStruct *p)
{

}

void AfterAlignmentInit(ArgStruct *p)
{

}

void MyMalloc(ArgStruct *p, int bufflen, int soffset, int roffset)
{
   void* buff1;
   void* buff2;
   
   // printf ("\nPE:%d MyMalloc p->bufflen:%d soffset:%d roffset:%d\n", shmem_my_pe(), p->bufflen, soffset, roffset);

   if((buff1=(char *)shmalloc(bufflen+MAX(soffset,roffset)))==(char *)NULL)
   {
      fprintf(stderr,"Error Message: Couldn't allocate memory\n");
      exit(-1);
   }

   if(!p->cache)

     if((buff2=(char *)shmalloc(bufflen+soffset))==(char *)NULL)
       {
         fprintf(stderr,"Error Message: Couldn't allocate memory\n");
         exit(-1);
       }

   if(p->cache) {
     p->r_buff = buff1;
   } else { /* Flip-flop buffers so send <--> recv between nodes */
     p->r_buff = p->tr ? buff1 : buff2;
     p->s_buff = p->tr ? buff2 : buff1;
   }

}
void FreeBuff(char *buff1, char* buff2)
{
  // printf ("\nPE:%d FreeBuff buff1:%p buff2:%p\n", shmem_my_pe(), buff1, buff2);
  if(buff1 != NULL)
    shfree(buff1);

  if(buff2 != NULL)
    shfree(buff2);
}
