#include "util/crash_report.hpp"

extern "C" {

alignas(16) u8 __nx_exception_stack[0x10000];
u64 __nx_exception_stack_size = sizeof(__nx_exception_stack);

void __libnx_exception_handler(ThreadExceptionDump* dump)
{
    akira::CrashReport::onException(*dump);
}

}
