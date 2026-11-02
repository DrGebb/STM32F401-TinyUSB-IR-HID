#include "utils_LL.h"
#include "stm32f4xx_ll_rcc.h"
#include "stm32f4xx_ll_system.h"
#include "stm32f4xx_ll_utils.h"
#include <stdbool.h>

void HSE_SetClock84Mhz(void) {

  // en HSE
  LL_RCC_HSE_Enable();
  while (LL_RCC_HSE_IsReady() != true)
    __NOP();

  // fill struct. EXTERN CRYSTAL 25MHZ
  LL_UTILS_PLLInitTypeDef PLL_init = {0};
  PLL_init.PLLM = LL_RCC_PLLM_DIV_25;
  PLL_init.PLLN = 336;
  PLL_init.PLLP = LL_RCC_PLLP_DIV_4;

  LL_UTILS_ClkInitTypeDef CLKbus_init = {0};
  CLKbus_init.AHBCLKDivider = LL_RCC_SYSCLK_DIV_1;
  CLKbus_init.APB2CLKDivider = LL_RCC_APB2_DIV_1;
  CLKbus_init.APB1CLKDivider = LL_RCC_APB1_DIV_2; // apb1 must be <= 42Mhz

  if (LL_PLL_ConfigSystemClock_HSE(25000000, LL_UTILS_HSEBYPASS_OFF, &PLL_init,
                                   &CLKbus_init) != SUCCESS) {

    while (1) {
    }
  }

  LL_RCC_HSE_EnableCSS();
}

void GetEnablePRF_InstDataCache(void) {

  LL_FLASH_EnablePrefetch();
  LL_FLASH_EnableDataCache();
  LL_FLASH_EnableInstCache();
}

void SystemClock_Config(void) {
  // 1. Enable HSE (external crystal oscillator)
  LL_RCC_HSE_Enable();
  while (!LL_RCC_HSE_IsReady())
    ;

  // 2. Configure Flash Latency
  // For 84 MHz and 3.3V, 2 wait states (WS) are required
  LL_FLASH_SetLatency(LL_FLASH_LATENCY_2);
  // Always verify that the setting has been applied
  while (LL_FLASH_GetLatency() != LL_FLASH_LATENCY_2)
    ;

  // 3. Enable PREFETCH and other features (optional, but recommended)
  LL_FLASH_EnablePrefetch();
  LL_FLASH_EnableInstCache();
  LL_FLASH_EnableDataCache();

  // 4. Configure the PLL
  // M = 25, N = 336, P = 4 (for 84 MHz), Q = 7 (for 48 MHz USB)
  // IMPORTANT: We configure the Main PLL for both domains (SYS and 48M)
  // simultaneously

  LL_RCC_PLL_Disable();
  while (LL_RCC_PLL_IsReady())
    ;

  // Main configuration (Source, M, N, P)
  LL_RCC_PLL_ConfigDomain_SYS(LL_RCC_PLLSOURCE_HSE, LL_RCC_PLLM_DIV_25, 336,
                              LL_RCC_PLLP_DIV_4);
  // Additional configuration for USB (Q)
  // This function modifies the SAME PLLCFGR register, adding the Q divider
  LL_RCC_PLL_ConfigDomain_48M(LL_RCC_PLLSOURCE_HSE, LL_RCC_PLLM_DIV_25, 336,
                              LL_RCC_PLLQ_DIV_7);

  // 5. Enable the PLL
  LL_RCC_PLL_Enable();
  while (!LL_RCC_PLL_IsReady())
    ;

  // 6. Switch the system clock source to the PLL
  LL_RCC_SetSysClkSource(LL_RCC_SYS_CLKSOURCE_PLL);
  while (LL_RCC_GetSysClkSource() != LL_RCC_SYS_CLKSOURCE_STATUS_PLL)
    ;

  // 7. Configure bus prescalers (AHB, APB1, APB2)
  LL_RCC_SetAHBPrescaler(LL_RCC_SYSCLK_DIV_1);
  LL_RCC_SetAPB1Prescaler(LL_RCC_APB1_DIV_2); // 42 MHz (max for APB1 is 42)
  LL_RCC_SetAPB2Prescaler(LL_RCC_APB2_DIV_1); // 84 MHz

  // 8. Update the global variable SystemCoreClock (for RTOS and UART)
  SystemCoreClockUpdate();
}