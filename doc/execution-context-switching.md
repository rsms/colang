# Execution context switching

M has a context
T has a context

m_call is called on a coroutine task T to switch execution to its M.


go mcall
  mcall switches from the g to the g0 stack and invokes fn(g),
  where g is the goroutine that made the call.
  mcall saves g's current PC/SP in g->sched so that it can be restored later.
  It is up to fn to arrange for that later execution, typically by recording
  g in a data structure, causing something to call ready(g) later.
  mcall returns to the original goroutine g later, when g has been rescheduled.
  fn must not return at all; typically it ends by calling schedule, to let the m
  run other goroutines.

