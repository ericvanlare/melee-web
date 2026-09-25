// Reuse the complete validated HSD prefix in this fresh process. The include
// keeps its argument validation and source allocation calls identical.
#define main source_hsd_startup_main
#include "original_startup_alloc_trace.cpp"
#undef main
#include "source_devcom_context.h"

int main(int argc, char** argv)
{
    const int status = source_hsd_startup_main(argc, argv);
    if (status != 0) return status;
    const Args args = parse(argc, argv);
    return melee_web_source_devcom_run(args.aram, args.aram_base);
}
