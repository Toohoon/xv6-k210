#include "../kernel/include/fcntl.h"
#include "../kernel/include/types.h"
#include "syscall.h"
#include "user.h"

int main() {
  stat_syscall();
  printf("\n");
  stat_syscall();
  printf("\n");
  stat_syscall();
  printf("\n");
  return 0;
}