#include "types.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "mmu.h"
#include "x86.h"
#include "proc.h"
#include "spinlock.h"
#include "stddef.h"

struct {
  struct spinlock lock;
  struct proc proc[NPROC];
} ptable;

uint multiLayeredFlag=0;

static struct proc *initproc;

int nextpid = 1;
extern void forkret(void);
extern void trapret(void);

static void wakeup1(void *chan);
unsigned int rand(void);

void
pinit(void)
{
  initlock(&ptable.lock, "ptable");
}

// Must be called with interrupts disabled
int
cpuid() {
  return mycpu()-cpus;
}

// Must be called with interrupts disabled to avoid the caller being
// rescheduled between reading lapicid and running through the loop.
struct cpu*
mycpu(void)
{
  int apicid, i;
  
  if(readeflags()&FL_IF)
    panic("mycpu called with interrupts enabled\n");
  
  apicid = lapicid();
  // APIC IDs are not guaranteed to be contiguous. Maybe we should have
  // a reverse map, or reserve a register to store &cpus[i].
  for (i = 0; i < ncpu; ++i) {
    if (cpus[i].apicid == apicid)
      return &cpus[i];
  }
  panic("unknown apicid\n");
}

// Disable interrupts so that we are not rescheduled
// while reading proc from the cpu structure
struct proc*
myproc(void) {
  struct cpu *c;
  struct proc *p;
  pushcli();
  c = mycpu();
  p = c->proc;
  popcli();
  return p;
}

//PAGEBREAK: 32
// Look in the process table for an UNUSED proc.
// If found, change state to EMBRYO and initialize
// state required to run in the kernel.
// Otherwise return 0.
static struct proc*
allocproc(void)
{
  struct proc *p;
  char *sp;

  acquire(&ptable.lock);

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++)
    if(p->state == UNUSED)
      goto found;

  release(&ptable.lock);
  return 0;

found:
 
  p->creationTime=ticks;//// changed
  p->state = EMBRYO;
  p->pid = nextpid++;
  ///alt

  p->priority=3; ////default priority
  p->queqeNumber=1;
  ////alt
  release(&ptable.lock);

  // Allocate kernel stack.
  if((p->kstack = kalloc()) == 0){
    p->state = UNUSED;
    return 0;
  }
  sp = p->kstack + KSTACKSIZE;

  // Leave room for trap frame.
  sp -= sizeof *p->tf;
  p->tf = (struct trapframe*)sp;

  // Set up new context to start executing at forkret,
  // which returns to trapret.
  sp -= 4;
  *(uint*)sp = (uint)trapret;

  sp -= sizeof *p->context;
  p->context = (struct context*)sp;
  memset(p->context, 0, sizeof *p->context);
  p->context->eip = (uint)forkret;

  return p;
}

//PAGEBREAK: 32
// Set up first user process.
void
userinit(void)
{
  struct proc *p;
  extern char _binary_initcode_start[], _binary_initcode_size[];

  p = allocproc();
  
  initproc = p;
  if((p->pgdir = setupkvm()) == 0)
    panic("userinit: out of memory?");
  inituvm(p->pgdir, _binary_initcode_start, (int)_binary_initcode_size);
  p->sz = PGSIZE;
  memset(p->tf, 0, sizeof(*p->tf));
  p->tf->cs = (SEG_UCODE << 3) | DPL_USER;
  p->tf->ds = (SEG_UDATA << 3) | DPL_USER;
  p->tf->es = p->tf->ds;
  p->tf->ss = p->tf->ds;
  p->tf->eflags = FL_IF;
  p->tf->esp = PGSIZE;
  p->tf->eip = 0;  // beginning of initcode.S

  safestrcpy(p->name, "initcode", sizeof(p->name));
  p->cwd = namei("/");

  // this assignment to p->state lets other cores
  // run this process. the acquire forces the above
  // writes to be visible, and the lock is also needed
  // because the assignment might not be atomic.
  acquire(&ptable.lock);

  p->state = RUNNABLE;
  


  release(&ptable.lock);
}

// Grow current process's memory by n bytes.
// Return 0 on success, -1 on failure.
int
growproc(int n)
{
  uint sz;
  struct proc *curproc = myproc();

  sz = curproc->sz;
  if(n > 0){
    if((sz = allocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  } else if(n < 0){
    if((sz = deallocuvm(curproc->pgdir, sz, sz + n)) == 0)
      return -1;
  }
  curproc->sz = sz;
  switchuvm(curproc);
  return 0;
}

// Create a new process copying p as the parent.
// Sets up stack to return as if from system call.
// Caller must set state of returned proc to RUNNABLE.
int
fork(void)
{
  int i, pid;
  struct proc *np;
  struct proc *curproc = myproc();

  // Allocate process.
  if((np = allocproc()) == 0){
    return -1;
  }

  // Copy process state from proc.
  if((np->pgdir = copyuvm(curproc->pgdir, curproc->sz)) == 0){
    kfree(np->kstack);
    np->kstack = 0;
    np->state = UNUSED;
    return -1;
  }
  np->sz = curproc->sz;
  np->parent = curproc;
  *np->tf = *curproc->tf;

  // Clear %eax so that fork returns 0 in the child.
  np->tf->eax = 0;

  for(i = 0; i < NOFILE; i++)
    if(curproc->ofile[i])
      np->ofile[i] = filedup(curproc->ofile[i]);
  np->cwd = idup(curproc->cwd);

  safestrcpy(np->name, curproc->name, sizeof(curproc->name));

  pid = np->pid;

  acquire(&ptable.lock);

  np->state = RUNNABLE;
  


  release(&ptable.lock);

  return pid;
}

// Exit the current process.  Does not return.
// An exited process remains in the zombie state
// until its parent calls wait() to find out it exited.
void
exit(void)
{
  struct proc *curproc = myproc();
  struct proc *p;
  int fd;

  if(curproc == initproc)
    panic("init exiting");

  // Close all open files.
  for(fd = 0; fd < NOFILE; fd++){
    if(curproc->ofile[fd]){
      fileclose(curproc->ofile[fd]);
      curproc->ofile[fd] = 0;
    }
  }

  begin_op();
  iput(curproc->cwd);
  end_op();
  curproc->cwd = 0;

  acquire(&ptable.lock);

  // Parent might be sleeping in wait().
  wakeup1(curproc->parent);

  // Pass abandoned children to init.
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->parent == curproc){
      p->parent = initproc;
      if(p->state == ZOMBIE)
        wakeup1(initproc);
    }
  }

  // Jump into the scheduler, never to return.
  /////alt
  curproc->terminationTime=ticks;
  /////alt
  curproc->state = ZOMBIE;
  sched();
  panic("zombie exit");
}

// Wait for a child process to exit and return its pid.
// Return -1 if this process has no children.
int
wait(int * cpuBurst , int * turnaround , int * waiting)
{
  struct proc *p;
  int havekids, pid;
  struct proc *curproc = myproc();
 // int counter=0;
  acquire(&ptable.lock);
  for(;;){
    // Scan through table looking for exited children.
    havekids = 0;
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
      //counter++;
      if(p->parent != curproc)
        continue;
      havekids = 1;
      //cprintf("cbt before zombie %d",p->runningTime);
      if(p->state == ZOMBIE){

        if(policy == 1 || policy==2 || policy==3){
          
          *cpuBurst=p->runningTime;
          *turnaround=p->readyTime + p->sleepingTime + p->runningTime;
          *waiting=p->readyTime + p->sleepingTime;
        }
        // Found one.
       // cprintf("cbt after zombie %d",p->runningTime);
        pid = p->pid;
        kfree(p->kstack);
        p->kstack = 0;
        freevm(p->pgdir);
        p->pid = 0;
        p->parent = 0;
        p->name[0] = 0;
        p->killed = 0;
        p->runningTime=0;
        p->readyTime=0;
        p->sleepingTime=0;
        p->state = UNUSED;
        release(&ptable.lock);
          ////for multiLayeredScheduling
        
          if(multiLayeredFlag!=0)
            return p->queqeNumber;

          else{

            if(policy==2 || policy==3)
              return p->priority;

            if(policy==0 || policy==1)
              return pid;


          }

      }
    }

    // No point waiting if we don't have any children.
    if(!havekids || curproc->killed){
      release(&ptable.lock);
      return -1;
    }

    // Wait for children to exit.  (See wakeup1 call in proc_exit.)
    sleep(curproc, &ptable.lock);  //DOC: wait-sleep
  }
}



//PAGEBREAK: 42
// Per-CPU process scheduler.
// Each CPU calls scheduler() after setting itself up.
// Scheduler never returns.  It loops, doing:
//  - choose a process to run
//  - swtch to start running that process
//  - eventually that process transfers control
//      via swtch back to the scheduler.
// void

// void
// scheduler(void)
// {
//   struct proc *p;
//   struct cpu *c = mycpu();
//   c->proc = 0;
 
//   struct proc *iterator;  //added by us
//   for(;;){
//     // Enable interrupts on this processor.
//     sti();
    
//   struct proc *highestPriority;/////////////////////////////////
//     // Loop over process table looking for process to run.
//   acquire(&ptable.lock);
//     for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
//       if(p->state != RUNNABLE)
//         continue;
//     if(policy ==2){
//       highestPriority=p;////////////////////

//       for(iterator= ptable.proc; iterator < &ptable.proc[NPROC]; iterator++){////////// find the highest priority
//         if(iterator->state != RUNNABLE)
//           continue;
//         if((iterator->priority)<(highestPriority->priority))
//           highestPriority=iterator;
//       }///////////////////////////

//       p=highestPriority;///////////


//     }
//        // Switch to chosen process.  It is the process's job
//       // to release ptable.lock and then reacquire it
//       // before jumping back to us.
//       c->proc = p;
//       switchuvm(p);
//       p->state = RUNNING;
     
//       //alt
//       if(policy==1)
//         p->current_slice = QUANTUM; ///ADDED BY US

//        ///alt
//       swtch(&(c->scheduler), p->context);
//       switchkvm();

//       // Process is done running for now.
//       // It should have changed its p->state before coming back.
//       c->proc = 0;
//     }
//     release(&ptable.lock);

//   }
// }

//MULTILAYERED QUEQE SCHEDULER;
void
scheduler(void)
{
  

  //if(multiLayeredFlag==0){

    struct proc *p;
    struct cpu *c = mycpu();
    c->proc = 0;
    int i=1;
    int found=0;
    struct proc *iterator;  //added by us
    for(;;){
      // Enable interrupts on this processor.
      sti();
      
    struct proc *highestPriority;/////////////////////////////////
      // Loop over process table looking for process to run.
      i=1;
      acquire(&ptable.lock);
      if(multiLayeredFlag==0){
        for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
          if(p->state != RUNNABLE)
            continue;
        if(policy ==2){
          highestPriority=p;////////////////////

          for(iterator= ptable.proc; iterator < &ptable.proc[NPROC]; iterator++){////////// find the highest priority
            if(iterator->state != RUNNABLE)
              continue;
            if((iterator->priority)<(highestPriority->priority))
              highestPriority=iterator;
          }///////////////////////////

          p=highestPriority;///////////


        }
          // Switch to chosen process.  It is the process's job
          // to release ptable.lock and then reacquire it
          // before jumping back to us.
          c->proc = p;
          switchuvm(p);
          p->state = RUNNING;
        
          //alt
          if(policy==1)
            p->current_slice = QUANTUM; ///ADDED BY US

          ///alt
          swtch(&(c->scheduler), p->context);
          switchkvm();

          // Process is done running for now.
          // It should have changed its p->state before coming back.
          c->proc = 0;
        }
      }else{

         while(i<=4){
        
        switch (i){

          case 1:
            policy=0;
            break;

          case 2:
            policy=2;
            break;


          case 3:
            policy=3;
            break;

          case 4:
            
            policy=1;
            break;


        }
      
      struct proc *highestPriority;/////////////////////////////////
      // Loop over process table looking for process to run.
      found=0;
      for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
        if(p->state != RUNNABLE || p->queqeNumber!=i)
          continue;
          
        // if(p->queqeNumber!=i)
        //   continue;
       
        if(policy ==2 || policy==3){  ////priority and reverese priority
         
          highestPriority=p;////////////////////

          for(iterator= ptable.proc; iterator < &ptable.proc[NPROC]; iterator++){////////// find the highest priority
            if(iterator->state != RUNNABLE || p->queqeNumber!=i)
              continue;
            if(policy==2) { 
              if((iterator->priority)<(highestPriority->priority))
                highestPriority=iterator;
            }else{

              if((iterator->priority)>(highestPriority->priority))
                highestPriority=iterator;
            }
          }///////////////////////////

          p=highestPriority;///////////


        }
        found=1;
        //cprintf("fffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff");
        // Switch to chosen process.  It is the process's job
        // to release ptable.lock and then reacquire it
        // before jumping back to us.
        c->proc = p;
        switchuvm(p);
        p->state = RUNNING;
      
        //alt
        if(policy==1)
          p->current_slice = QUANTUM; ///ADDED BY US

        ///alt
        swtch(&(c->scheduler), p->context);
        switchkvm();

        // Process is done running for now.
        // It should have changed its p->state before coming back.
        
        c->proc = 0;
      }
        if(found==0)
        i++;
      }
    
      
      // if(i==5)
      //   i=1;

      }
      release(&ptable.lock);

    }



 
   
  }






// Enter scheduler.  Must hold only ptable.lock
// and have changed proc->state. Saves and restores
// intena because intena is a property of this
// kernel thread, not this CPU. It should
// be proc->intena and proc->ncli, but that would
// break in the few places where a lock is held but
// there's no process.
void
sched(void)
{
  int intena;
  struct proc *p = myproc();

  if(!holding(&ptable.lock))
    panic("sched ptable.lock");
  if(mycpu()->ncli != 1)
    panic("sched locks");
  if(p->state == RUNNING)
    panic("sched running");
  if(readeflags()&FL_IF)
    panic("sched interruptible");
  intena = mycpu()->intena;
  swtch(&p->context, mycpu()->scheduler);
  mycpu()->intena = intena;
}

// Give up the CPU for one scheduling round.
void
yield(void)
{
  acquire(&ptable.lock);  //DOC: yieldlock
  myproc()->state = RUNNABLE;
  sched();
  release(&ptable.lock);
}

// A fork child's very first scheduling by scheduler()
// will swtch here.  "Return" to user space.
void
forkret(void)
{
  static int first = 1;
  // Still holding ptable.lock from scheduler.
  release(&ptable.lock);

  if (first) {
    // Some initialization functions must be run in the context
    // of a regular process (e.g., they call sleep), and thus cannot
    // be run from main().
    first = 0;
    iinit(ROOTDEV);
    initlog(ROOTDEV);
  }

  // Return to "caller", actually trapret (see allocproc).
}

// Atomically release lock and sleep on chan.
// Reacquires lock when awakened.
void
sleep(void *chan, struct spinlock *lk)
{
  struct proc *p = myproc();
  
  if(p == 0)
    panic("sleep");

  if(lk == 0)
    panic("sleep without lk");

  // Must acquire ptable.lock in order to
  // change p->state and then call sched.
  // Once we hold ptable.lock, we can be
  // guaranteed that we won't miss any wakeup
  // (wakeup runs with ptable.lock locked),
  // so it's okay to release lk.
  if(lk != &ptable.lock){  //DOC: sleeplock0
    acquire(&ptable.lock);  //DOC: sleeplock1
    release(lk);
  }
  // Go to sleep.
  p->chan = chan;
  p->state = SLEEPING;
 

  sched();

  // Tidy up.
  p->chan = 0;

  // Reacquire original lock.
  if(lk != &ptable.lock){  //DOC: sleeplock2
    release(&ptable.lock);
    acquire(lk);
  }
}

//PAGEBREAK!
// Wake up all processes sleeping on chan.
// The ptable lock must be held.
static void
wakeup1(void *chan)
{
  struct proc *p;

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->state == SLEEPING && p->chan == chan){
      p->state = RUNNABLE;
     
      
    }
  }
}

// Wake up all processes sleeping on chan.
void
wakeup(void *chan)
{
  acquire(&ptable.lock);
  wakeup1(chan);
  release(&ptable.lock);
}

// Kill the process with the given pid.
// Process won't exit until it returns
// to user space (see trap in trap.c).
int
kill(int pid)
{
  struct proc *p;

  acquire(&ptable.lock);
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->pid == pid){
      p->killed = 1;
      // Wake process from sleep if necessary.
      if(p->state == SLEEPING){
        p->state = RUNNABLE;
      }
      release(&ptable.lock);
      return 0;
    }
  }
  release(&ptable.lock);
  return -1;
}

//PAGEBREAK: 36
// Print a process listing to console.  For debugging.
// Runs when user types ^P on console.
// No lock to avoid wedging a stuck machine further.
void
procdump(void)
{
  static char *states[] = {
  [UNUSED]    "unused",
  [EMBRYO]    "embryo",
  [SLEEPING]  "sleep ",
  [RUNNABLE]  "runble",
  [RUNNING]   "run   ",
  [ZOMBIE]    "zombie"
  };
  int i;
  struct proc *p;
  char *state;
  uint pc[10];

  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
    if(p->state == UNUSED)
      continue;
    if(p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";
    cprintf("%d %s %s", p->pid, state, p->name);
    if(p->state == SLEEPING){
      getcallerpcs((uint*)p->context->ebp+2, pc);
      for(i=0; i<10 && pc[i] != 0; i++)
        cprintf(" %p", pc[i]);
    }
    cprintf("\n");
  }
}

//ADDED CODE BY US
///methods

// static long randstate = 1;
// unsigned int
// rand()
// {
//   randstate = randstate * 1664525 + 1013904223;
//   return randstate;
// }

// int findProcIndex (int inputPID){

//   struct proc *p;
//   int counter=0;
//   acquire(&ptable.lock);
//     for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
//         counter ++;
//         if(p->pid == inputPID){
//           break;
//         }
      
    
//   }
//   release(&ptable.lock);
//   return counter;
// }


void processingTimeVariables(void){

  struct proc *p;

  acquire(&ptable.lock);
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){

      switch (p->state){

      case RUNNING:
        (p->runningTime)++;
        break;

      case SLEEPING:
        (p->sleepingTime)++;
        break;

      case RUNNABLE:
        (p->readyTime)++;
        break;
     
      default :
        break;
      }
  }
  release(&ptable.lock);

}


//system calls
int getParentID (){

  struct proc *curproc = myproc();
  return curproc->parent->pid;

}

int getChildren(){

  struct proc *p;
  struct proc *curproc = myproc();
  int counter=0;
  int multiplier=1;
  int children=0;
 
  acquire(&ptable.lock);
  for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){

    if((p->parent->pid) == (curproc->pid)){
      
      for(int i=1 ; i<=counter ; i++){

          multiplier*=100;


    }
      
      children+=((p->pid) * multiplier);
      multiplier=1;
      counter++;
    }
    
  }
  release(&ptable.lock);
 
 
  return children;

}
int getSyscallCounter(int number){

  struct proc *curproc = myproc();

  return curproc->numsyscall[number-1];
}


int setPriority(int inPriority){

  if(inPriority>6 || inPriority<1){
    inPriority=5;
  }
  struct proc *curproc = myproc();
  curproc->priority=inPriority;

  return curproc->priority; ///???

}
int getPriority(){
   
  struct proc *curproc = myproc();
  return curproc->priority; ///???

}
int changePolicy(int plcy){
  
  if(plcy>3 || plcy<0){ ////  3 for  reverse priority
    plcy=0;
  }
  policy=plcy;
  return policy;
}



int getPriorityOfPID(int ID){
  struct proc *p;
  
  acquire(&ptable.lock);
    for(p = ptable.proc; p < &ptable.proc[NPROC]; p++){
        
        if(p->pid == ID){

          break;
        }
      
    
  }
  release(&ptable.lock);

  return p->priority;
}



int setQueqeNumber(int qNum){

  struct proc *curproc = myproc();

  if(qNum>4 || qNum<1){ 
    qNum=1;
  }
  curproc->queqeNumber=qNum;
  
  return curproc->queqeNumber;

}
int changeMultiFlag(int input){
  
  if(input>3 || input<0){ ////  3 for  reverse priority
    input=0;
  }
  multiLayeredFlag=input;
  return multiLayeredFlag;
}