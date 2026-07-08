#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/param.h"
#include "user/user.h"

void
run(char *line, int argc, char *argv[])
{
  char *args[MAXARG];
  int narg = 0;

  for(int i = 1; i < argc; i++){
    args[narg++] = argv[i];
  }

  char *p = line;
  while(*p){
    while(*p == ' ' || *p == '\t'){
      *p = 0;
      p++;
    }

    if(*p == 0)
      break;

    if(narg >= MAXARG - 1){
      fprintf(2, "xargs: too many arguments\n");
      exit(1);
    }

    args[narg++] = p;

    while(*p && *p != ' ' && *p != '\t')
      p++;
  }

  args[narg] = 0;

  int pid = fork();

  if(pid < 0){
    fprintf(2, "xargs: fork error\n");
    exit(1);
  }

  if(pid == 0){
    exec(args[0], args);
    fprintf(2, "xargs: exec %s failed\n", args[0]);
    exit(1);
  } else {
    wait(0);
  }
}

int
main(int argc, char *argv[])
{
  if(argc < 2){
    fprintf(2, "usage: xargs command [args...]\n");
    exit(1);
  }

  char buf[512];
  char c;
  int n = 0;

  while(read(0, &c, 1) == 1){
    if(c == '\n'){
      buf[n] = 0;

      if(n > 0)
        run(buf, argc, argv);

      n = 0;
    } else {
      if(n < sizeof(buf) - 1){
        buf[n++] = c;
      }
    }
  }

  if(n > 0){
    buf[n] = 0;
    run(buf, argc, argv);
  }

  exit(0);
}
