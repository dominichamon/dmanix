#include "syscall.h"

#include "array_size.h"
#include "assert.h"
#include "isr.h"
#include "screen.h"

#define DEFN_SYSCALL0(fn, num)                      \
int _##fn() {                                       \
  int a;                                            \
  asm volatile("int $0x80" : "=a" (a) : "0" (num)); \
  return a;                                         \
}

#define DEFN_SYSCALL1(fn, num, P1)                                  \
int _##fn(P1 p1) {                                                  \
  int a;                                                            \
  asm volatile("int $0x80" : "=a" (a) : "0" (num), "b" ((int)p1));  \
  return a;                                                         \
}

#define DEFN_SYSCALL2(fn, num, P1, P2)            \
int _##fn(P1 p1, P2 p2) {                         \
  int a;                                          \
  asm volatile("int $0x80" : "=a" (a)             \
      : "0" (num), "b" ((int)p1), "c" ((int)p2)); \
  return a;                                       \
}

namespace syscall {

DEFN_SYSCALL1(puts, 0, const char*)

namespace {

void* syscalls[] = {
  (void*) &_puts
};

void Handler(isr::Registers& regs) {
  ASSERT(regs.eax < ARRAY_SIZE(syscalls));

  void* location = syscalls[regs.eax];

  // Push all the parameters in the correct order.
  int ret;
  asm volatile (" \
      push %1;    \
      push %2;    \
      push %3;    \
      push %4;    \
      push %5;    \
      call *%6;   \
      pop %%ebx;  \
      pop %%ebx;  \
      pop %%ebx;  \
      pop %%ebx;  \
      pop %%ebx"
      : "=a" (ret)
      : "r" (regs.edi), "r" (regs.esi), "r" (regs.edx), "r" (regs.ecx),
        "r" (regs.ebx), "r" (location));
  regs.eax = ret;
}

}  // namespace

void Initialize() {
  isr::RegisterHandler(0x80, Handler);
}

}  // namespace syscall