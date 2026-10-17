#include "gd32f30x.h"

/* Keep the power-on IRC8M clock until the board oscillator is known. */
uint32_t SystemCoreClock = 8000000U;

void SystemInit(void)
{
}

void SystemCoreClockUpdate(void)
{
    SystemCoreClock = 8000000U;
}
