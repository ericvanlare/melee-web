#include <stdint.h>

volatile uint32_t renderer_host_state = UINT32_C(0xabcdef01);

uintptr_t renderer_host_state_address(void)
{
    return (uintptr_t)&renderer_host_state;
}
