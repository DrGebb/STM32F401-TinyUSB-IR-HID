#ifndef UTILS_LL_H
#define UTILS_LL_H

#include "RTE_Components.h"
#include CMSIS_device_header
#include "stm32f4xx_ll_utils.h"
#include "stm32f4xx_ll_rcc.h"
#include "stm32f4xx_ll_system.h"


//utils, system LL drivers uses

void HSE_SetClock84Mhz(void);

void GetEnablePRF_InstDataCache(void);

void SystemClock_Config(void);



#endif