#include "types.h"
#include "riscv.h"
#include "param.h"
#include "defs.h"
#include "date.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"

uint64
sys_exit(void)
{
  int n;
  if(argint(0, &n) < 0)
    return -1;
  exit(n);
  return 0;  // not reached
}

uint64
sys_getpid(void)
{
  return myproc()->pid;
}

uint64
sys_fork(void)
{
  return fork();
}

uint64
sys_wait(void)
{
  uint64 p;
  if(argaddr(0, &p) < 0)
    return -1;
  return wait(p);
}

uint64
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

uint64
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


#ifdef LAB_PGTBL
int
sys_pgaccess(void)
{
  uint64 start;
  int npages;
  uint64 user_mask;
  uint mask = 0;
  struct proc *p = myproc();

  // 获取三个系统调用参数：
  // start：起始虚拟地址
  // npages：检查的页面数量
  // user_mask：用户空间中保存结果的地址
  if(argaddr(0, &start) < 0)
    return -1;
  if(argint(1, &npages) < 0)
    return -1;
  if(argaddr(2, &user_mask) < 0)
    return -1;

  // uint 一共 32 位，因此最多检查 32 个页面。
  if(npages < 0 || npages > 32)
    return -1;

  for(int i = 0; i < npages; i++){
    uint64 va = start + (uint64)i * PGSIZE;

    // 找到这个虚拟地址所对应的页表项。
    // 第三个参数为 0，表示找不到时不要创建新页表。
    pte_t *pte = walk(p->pagetable, va, 0);

    if(pte == 0 || (*pte & PTE_V) == 0)
      continue;

    // 检查该页面是否被访问。
    if(*pte & PTE_A){
      mask |= (1U << i);

      // 检查完成后清除访问位。
      *pte &= ~PTE_A;
    }
  }

  // 将内核中的位掩码复制回用户空间。
  if(copyout(p->pagetable, user_mask,
             (char *)&mask, sizeof(mask)) < 0)
    return -1;

  return 0;
}
#endif

uint64
sys_kill(void)
{
  int pid;

  if(argint(0, &pid) < 0)
    return -1;
  return kill(pid);
}

// return how many clock tick interrupts have occurred
// since start.
uint64
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}
