#include "types.h"
#include "x86.h"
#include "defs.h"
#include "date.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "proc.h"

int
sys_fork(void)
{
  return fork();
}

int
sys_exit(void)
{
  exit();
  return 0;  // not reached
}

int
sys_wait(void)
{


  int *cpuBurst, *turnaround, *waiting;
  if (argptr(0, (void*)&cpuBurst, sizeof(cpuBurst)) < 0)
    return -1;
  if (argptr(1, (void*)&turnaround, sizeof(turnaround)) < 0)
    return -1;
  if (argptr(2, (void*)&waiting, sizeof(waiting)) < 0)
    return -1;
  return wait(cpuBurst,turnaround,waiting);
}

int
sys_kill(void)
{
  int pid;

  if(argint(0, &pid) < 0)
    return -1;
  return kill(pid);
}

int
sys_getpid(void)
{
  return myproc()->pid;
}

int
sys_sbrk(void)
{
  int addr;
  int n;

  if(argint(0, &n) < 0)
    return -1;
  addr = myproc()->sz;
  if(growproc(n) < 0)
    return -1;
  return addr;
}

int
sys_sleep(void)
{
  int n;
  uint ticks0;

  if(argint(0, &n) < 0)
    return -1;
  acquire(&tickslock);
  ticks0 = ticks;
  while(ticks - ticks0 < n){
    if(myproc()->killed){
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}

// return how many clock tick interrupts have occurred
// since start.
int
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}


//ADDED CODES BY US
int sys_getParentID(void){


  return getParentID();

}

int  sys_getChildren (void){

  return getChildren();


}
int  sys_getSyscallCounter(void){

  int n;

  if(argint(0, &n) < 0)
   return -1;
  return getSyscallCounter(n);

}

int sys_setPriority(void){

  int inPriority;
  
  if(argint(0, &inPriority) < 0)
   return -1;

  return setPriority(inPriority);

}

int sys_getPriority(void){

 
  return getPriority();

}
int sys_changePolicy(void){
  
  int plcy;
  
  if(argint(0, &plcy) < 0)
   return -1;
  return changePolicy(plcy);
}


int sys_getPriorityOfPID(void){
   int pid;
  if(argint(0, &pid) < 0)
   return -1;

  return getPriorityOfPID(pid);
}

int sys_setQueqeNumber(void){

  int qNum;
  if(argint(0, &qNum) < 0)
   return -1;

  return setQueqeNumber(qNum);
}


int sys_changeMultiFlag(void){

    int input;
    if(argint(0, &input) < 0)
    return -1;

    return changeMultiFlag(input);



}

