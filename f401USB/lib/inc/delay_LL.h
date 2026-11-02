#ifndef DELAY_LL_H
#define  DELAY_LL_H

#include "stm32f4xx_ll_cortex.h"
#include "stm32f4xx_ll_utils.h"

extern __IO uint32_t msTick; //msec tick

void Systick_LL_Init(void);

uint32_t GetSysTick(void);

void DelayMs(uint32_t delay_ms);







#endif