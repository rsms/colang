#include <rbase/rbase.h>
#include <rt/sched.h>

ASSUME_NONNULL_BEGIN

#define GREEN  "\e[1;32m"
#define YELLOW "\e[1;33m"
#define PURPLE "\e[1;35m"

#define REINTERPRET_CAST(dsttype, value) ({ \
  DIAGNOSTIC_IGNORE_PUSH(-Wstrict-aliasing) \
  __typeof__(value) v = value; dsttype v2 = *((dsttype*)&v); \
  DIAGNOSTIC_IGNORE_POP \
  v2; \
})


static void fn3(uintptr_t arg1) {
  dlog(PURPLE "fn3 coroutine. arg1=%f", REINTERPRET_CAST(double, arg1));
  dlog(PURPLE "EXIT");
}

static void fn2() {
  dlog(YELLOW "fn2 coroutine");
  dlog(YELLOW "spawn fn3");
  t_spawn(fn3, REINTERPRET_CAST(uintptr_t, 12.34));
  dlog(YELLOW "calling t_yield()");
  t_yield();
  dlog(YELLOW "back from yield");
  // // msleep(1000);
  dlog(YELLOW "EXIT");
}

static void fn1(uintptr_t arg1) {
  #define GREEN "\e[1;32m"
  dlog(GREEN "main coroutine. arg1=%zu", arg1);

  dlog(GREEN "spawn fn2");
  t_spawn(fn2, 0);

  // // t_spawn_custom(fn2, /*stackmem*/NULL, /*stacksize*/4096*4);

  // static u8 smolstack[4096];
  // t_spawn_custom(fn2, 0, 0, smolstack, sizeof(smolstack));

  // // test to ensure user stacks are correctly aligned:
  // t_spawn_custom(fn2, &smolstack[1], sizeof(smolstack)-1);

  // #define spawnb(expr) ({ \
  //   newproc((EntryFun)&&label1, NULL, 0, NULL, 0); \
  //   label1: expr; \
  // })
  // spawnb(fn3(123, 4.56));

  // msleep(1000);

  dlog(GREEN "calling t_yield()");
  t_yield();
  dlog(GREEN "back from yield; calling t_yield()");
  t_yield();
  dlog(GREEN "back from yield");

  dlog(GREEN "EXIT");
}

int main(int argc, const char* argv[argc+1]) {
  sched_main(fn1, 123); // never returns
  return 0;
}

ASSUME_NONNULL_END
