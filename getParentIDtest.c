#include "types.h"
//#include "defs.h"
#include "stat.h"
#include "user.h"
//#include "spinlock.h"


/*struct {
  struct spinlock lock;
  
} mylock;*/


int main (){

   
    //int pid,parentID;
    //int lock;
    //struct proc *curproc = myproc();

    for(int i=1 ; i<=5 ; i++){

        ///pid=getpid();

       if(fork()== 0){

           //pid=getpid();
           //parentID=getParentID();
           //acquire(&mylock.lock);
          
           printf(1,"This is process %d ",getpid());
           printf(1,"and the parent id is %d \n",getParentID());
           
           //release(&mylock.lock);

       }

    }


    /* wait for all child to terminate */
    while(wait() != -1) { }

    /* give time to parent to reach wait clause */
    sleep(1);

    exit();

    return 0;
}