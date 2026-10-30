#include <stdint.h>

volatile uint32_t project_owner_state = UINT32_C(0x87654321);

uintptr_t project_owner_state_address(void)
{
    return (uintptr_t)&project_owner_state;
}
