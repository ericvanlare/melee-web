#include <stdint.h>

extern uintptr_t source_initialized_address(void);
extern uintptr_t source_zero_address(void);

uintptr_t probe_source_initialized_address(void)
{
    return source_initialized_address();
}

uintptr_t probe_source_zero_address(void)
{
    return source_zero_address();
}
