#include "ir_nec.h"

__IO static NEC_Handle_t hNEC = {0};

// All times are in timer ticks (1 tick = 1 us), measured between two FALLING edges.
// Falling edge of the receiver output = start of a burst.
#define NEC_START_MIN       12500U  // leader: 9 ms + 4.5 ms = 13.5 ms
#define NEC_START_MAX       14500U
#define NEC_REPEAT_MIN      10500U  // repeat: 9 ms + 2.25 ms = 11.25 ms
#define NEC_REPEAT_MAX      12000U
#define NEC_BIT_MIN         800U    // bit 0: 1.125 ms
#define NEC_BIT_MAX         2800U   // bit 1: 2.25 ms
#define NEC_BIT_THRESHOLD   1700U   // ~ middle between 1125 and 2250

//PB0 TIM3 CH3 AF2
void NEC_IR_CaptureInit(void){

    //clk for gpiob
    LL_AHB1_GRP1_EnableClock(LL_AHB1_GRP1_PERIPH_GPIOB);

    LL_GPIO_InitTypeDef gpio_init = {0};
    gpio_init.Pin = LL_GPIO_PIN_0;
    gpio_init.Mode = LL_GPIO_MODE_ALTERNATE;
    gpio_init.OutputType = LL_GPIO_OUTPUT_PUSHPULL;
    gpio_init.Alternate = LL_GPIO_AF_2;
    gpio_init.Speed = LL_GPIO_SPEED_FREQ_LOW;
    gpio_init.Pull = LL_GPIO_PULL_UP;
    LL_GPIO_Init(GPIOB, &gpio_init);

    //tim3 conf
    LL_APB1_GRP1_EnableClock(LL_APB1_GRP1_PERIPH_TIM3);

    LL_TIM_InitTypeDef tim_init = {0};
    tim_init.Prescaler = (NEC_TIMER_CLK_HZ / 1000000U) - 1U; // 1 MHz timer clock
    tim_init.Autoreload = 0xFFFF;
    tim_init.CounterMode = LL_TIM_COUNTERMODE_UP;
    tim_init.ClockDivision = LL_TIM_CLOCKDIVISION_DIV4; // fDTS = fCK_INT / 4
    tim_init.RepetitionCounter = 0;
    LL_TIM_Init(TIM3, &tim_init);

    LL_TIM_IC_InitTypeDef IC_init = {0};
    IC_init.ICPrescaler = LL_TIM_ICPSC_DIV1;
    IC_init.ICActiveInput = LL_TIM_ACTIVEINPUT_DIRECTTI;
    IC_init.ICFilter = LL_TIM_IC_FILTER_FDIV32_N8; // 16 MHz: fDTS = 4 MHz -> 32 * 8 / 4 MHz = 64 us
    IC_init.ICPolarity = LL_TIM_IC_POLARITY_FALLING;
    LL_TIM_IC_Init(TIM3, LL_TIM_CHANNEL_CH3, &IC_init);

    //IT conf
    NVIC_SetPriority(TIM3_IRQn, 5);
    LL_TIM_ClearFlag_CC3(TIM3);
    LL_TIM_ClearFlag_CC3OVR(TIM3);
    LL_TIM_EnableIT_CC3(TIM3);
    NVIC_ClearPendingIRQ(TIM3_IRQn);
    NVIC_EnableIRQ(TIM3_IRQn);

    LL_TIM_CC_EnableChannel(TIM3, LL_TIM_CHANNEL_CH3);
    LL_TIM_EnableCounter(TIM3);
}

void TIM3_IRQHandler(void) {

    if (!LL_TIM_IsActiveFlag_CC3(TIM3)) return;

    LL_TIM_ClearFlag_CC3(TIM3);
    LL_TIM_ClearFlag_CC3OVR(TIM3); // lost capture: the frame will fail the timing checks anyway

    uint16_t now   = (uint16_t)LL_TIM_IC_GetCaptureCH3(TIM3);
    uint16_t delta = (uint16_t)(now - hNEC.Last); // modulo 2^16 arithmetic handles counter wrap
    hNEC.Last = now;

    switch (hNEC.status) {

        case NEC_IDLE:
            // first falling edge = possible start of the leader
            hNEC.status = NEC_WAIT_START;
            break;

        case NEC_WAIT_START:
            if (delta >= NEC_START_MIN && delta <= NEC_START_MAX) {
                hNEC.BitCount = 0;
                hNEC.Buffer = 0;
                hNEC.status = NEC_RECEIVING;
            }
            else if (delta >= NEC_REPEAT_MIN && delta <= NEC_REPEAT_MAX) {
                // repeat frame (key is held): 9 ms + 2.25 ms + stop burst
                if (hNEC.HasCode) {
                    if (hNEC.Repeat < 255) hNEC.Repeat++;
                    hNEC.RepeatReady = 1;
                    if (hNEC.Callback) hNEC.Callback();
                }
                hNEC.status = NEC_IDLE;
            }
            else {
                // bad timing: this edge may be the leader start of a new frame
                hNEC.status = NEC_WAIT_START;
            }
            break;

        case NEC_RECEIVING:
            if (delta >= NEC_BIT_MIN && delta <= NEC_BIT_MAX) {

                if (delta > NEC_BIT_THRESHOLD) hNEC.Buffer |= (1UL << hNEC.BitCount); // log 1, LSB first
                hNEC.BitCount++;

                if (hNEC.BitCount >= 32) {
                    // the 33rd falling edge is the stop burst, so the last bit is already measured
                    uint8_t cmd     = (uint8_t)(hNEC.Buffer >> 16);
                    uint8_t cmd_inv = (uint8_t)(hNEC.Buffer >> 24);

                    if (cmd == (uint8_t)~cmd_inv) {
                        hNEC.Code = hNEC.Buffer;
                        hNEC.Repeat = 0;
                        hNEC.RepeatReady = 0;
                        hNEC.HasCode = 1;
                        hNEC.Ready = 1;
                        if (hNEC.Callback) hNEC.Callback();
                    }
                    hNEC.status = NEC_IDLE;
                }
            }
            else {
                // bad timing: this edge may be the leader start of a new frame
                hNEC.status = NEC_WAIT_START;
            }
            break;

        default:
            hNEC.status = NEC_IDLE;
            break;
    }
}

// Returns 1 once per new valid frame. Repeat frames are reported via NEC_GetRepeatCount().
NEC_Event_t NEC_Read(uint16_t *addr, uint8_t *cmd){

    NEC_Event_t evt;

    if(hNEC.Ready){
        hNEC.Ready = 0;
        evt = NEC_EVT_NEW;
    }
    else if(hNEC.RepeatReady){
        hNEC.RepeatReady = 0;
        evt = NEC_EVT_REPEAT;
    }
    else return NEC_EVT_NONE;

    uint32_t raw = hNEC.Code;
    uint8_t a     = (uint8_t)raw;
    uint8_t a_inv = (uint8_t)(raw >> 8);

    if (addr) *addr = (a == (uint8_t)~a_inv) ? a : (uint16_t)(raw & 0xFFFF);
    if (cmd)  *cmd  = (uint8_t)(raw >> 16);

    return evt;
}

uint8_t NEC_GetRepeatCount(void){
    return hNEC.Repeat;
}

uint16_t NEC_GetAddress(void){

    uint32_t raw = hNEC.Code;
    uint8_t a     = (uint8_t)raw;         // bits 0..7
    uint8_t a_inv = (uint8_t)(raw >> 8);  // bits 8..15

    // standard NEC: addr + ~addr -> 8-bit address; extended NEC: full 16-bit address
    return (a == (uint8_t)~a_inv) ? a : (uint16_t)(raw & 0xFFFF);
}

void NEC_SetCallback(void (*cb)(void)) {
     hNEC.Callback = cb;
}

void NEC_Flush(void) {
     hNEC.Ready = 0; hNEC.RepeatReady = 0;
}