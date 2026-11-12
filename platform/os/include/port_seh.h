#ifndef PORT_SEH_H
#define PORT_SEH_H
#ifdef _WIN32
#define PORT_SEH_TRY __try
#define PORT_SEH_EXCEPT __except (EXCEPTION_EXECUTE_HANDLER)
#endif
#if !defined(_WIN32) && defined(__cplusplus)
#include <setjmp.h>

struct PortSehFrame {
  sigjmp_buf env;
  PortSehFrame* previous;
  PortSehFrame();
  ~PortSehFrame();
};
extern "C" PortSehFrame* PortSeh_Top(void);

#define PORT_SEH_TRY if (PortSehFrame portSehFrame; sigsetjmp(portSehFrame.env, 1) == 0)
#define PORT_SEH_EXCEPT else

#endif
#endif
