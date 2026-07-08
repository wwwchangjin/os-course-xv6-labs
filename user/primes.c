#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

void sieve(int left_fd)
{
    int prime;

    if(read(left_fd,&prime,sizeof(prime))==0)
    {
        close(left_fd);
        exit(0);
    }
    printf("prime %d\n",prime);

    int right[2];
    pipe(right);

    int pid=fork();

    if(pid==0)
    {
        close(right[1]);
        close(left_fd);
        sieve(right[0]);
        exit(0);
    }
    else
    {
        close(right[0]);
        int num;

        while(read(left_fd,&num,sizeof(num))!=0)
        {
            if(num%prime!=0)
            {
                write(right[1],&num,sizeof(num));
            }
        }
        close(left_fd);
        close(right[1]);
        wait(0);
        exit(0);
    }
}

int main(int argc,char *argv[])
{
    int p[2];
    pipe(p);

    int pid=fork();
    if(pid==0)
    {
        close(p[1]);
        sieve(p[0]);
        exit(0);
    }
    else
    {
        close(p[0]);  
        for(int i=2;i<=35;i++)
        {
            write(p[1],&i,sizeof(i));
        }
        close(p[1]);
        wait(0);
        exit(0);
    }
}