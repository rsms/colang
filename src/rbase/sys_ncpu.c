#include "rbase.h"

#if R_TARGET_OS_LINUX
  #include <sys/sysinfo.h>
#elif R_TARGET_OS_POSIX
  #include <sys/sysctl.h>
#endif

u32 sys_ncpu() {

// -----------------------------------------------------------------------------
#if R_TARGET_OS_WINDOWS
  SYSTEM_INFO sysinfo;
  GetSystemInfo(&sysinfo);
  // maybe use GetLogicalProcessorInformation instead?
  return (u32)sysinfo.dwNumberOfProcessors;

// -----------------------------------------------------------------------------
#elif R_TARGET_OS_LINUX
  //return (u32)sysconf(_SC_NPROCESSORS_CONF);
  return (u32)sysconf(_SC_NPROCESSORS_ONLN);

// -----------------------------------------------------------------------------
#elif R_TARGET_OS_POSIX
  int mib[4];
  mib[0] = CTL_HW;
  mib[1] = HW_AVAILCPU;
  int n;
  size_t len = sizeof(n);
  if (sysctl(mib, 2, &n, &len, NULL, 0) == 0 || n > 0)
    return (u32)n;
  // try HW_NCPU
  mib[1] = HW_NCPU;
  if (sysctl(mib, 2, &n, &len, NULL, 0) == 0 || n > 0)
    return (u32)n;

// -----------------------------------------------------------------------------
#else
  #warning "no implementation for this system"

  // HPUX?
  // int numCPU = mpctl(MPC_GETNUMSPUS, NULL, NULL);

  // IRIX?
  // int numCPU = sysconf(_SC_NPROC_ONLN);

  // macOS/iOS Objective-C Foundation
  // NSUInteger ncpu = [[NSProcessInfo processInfo] processorCount];
  // NSUInteger navailcpu = [[NSProcessInfo processInfo] activeProcessorCount];

#endif
  // 0 singals that the cpu count could not be determined
  return 0;
}
