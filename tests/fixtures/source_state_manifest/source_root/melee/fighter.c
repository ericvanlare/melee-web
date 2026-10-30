#include <stdint.h>

volatile uint32_t source_initialized = UINT32_C(0x12345678);
volatile uint32_t source_zero;

uintptr_t source_initialized_address(void)
{
    return (uintptr_t)&source_initialized;
}

uintptr_t source_zero_address(void)
{
    return (uintptr_t)&source_zero;
}
