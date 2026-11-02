#ifndef IR_NEC_H
#define IR_NEC_H

#include <stdint.h>
#include CMSIS_device_header
#include "stm32f4xx_ll_bus.h"
#include "stm32f4xx_ll_gpio.h"
#include "stm32f4xx_ll_tim.h"

#ifndef NEC_TIMER_CLK_HZ
#define NEC_TIMER_CLK_HZ 84000000U  // TIM3 input clock (APB1 timer clock)
#endif

typedef enum { 

    NEC_EVT_NONE = 0,
    NEC_EVT_NEW,
    NEC_EVT_REPEAT
    
} NEC_Event_t;


typedef enum {

    NEC_IDLE = 0,       // waiting for the first falling edge
    NEC_WAIT_START,     // got the first edge, waiting for the leader interval
    NEC_RECEIVING       // receiving 32 bits
    
} Status_NEC_t;

typedef struct {
    
    uint16_t Last;      // last captured counter value
    uint8_t  BitCount;
    uint32_t Buffer;    // frame being received
    uint32_t Code;      // last valid frame
    uint8_t  Repeat;    // repeat frames since the last valid frame
    uint8_t  HasCode;
    uint8_t  Ready;     // new valid frame not read yet
    uint8_t  RepeatReady;
    Status_NEC_t status;

} NEC_Handle_t;

void    NEC_IR_CaptureInit(void);
uint8_t NEC_GetRepeatCount(void);
uint16_t NEC_GetAddress(void);

NEC_Event_t NEC_Read(uint16_t *addr, uint8_t *cmd); 

#endif