#include "main.h"



// Initialize GPIO pins and peripheral clocks for USB OTG FS
void USB_LL_Init(void) {
    // Enable peripheral clocks
    LL_AHB1_GRP1_EnableClock(LL_AHB1_GRP1_PERIPH_GPIOA);
    LL_AHB2_GRP1_EnableClock(LL_AHB2_GRP1_PERIPH_OTGFS);
    LL_APB2_GRP1_EnableClock(LL_APB2_GRP1_PERIPH_SYSCFG);

    // PA11 - DM, PA12 - DP
    LL_GPIO_InitTypeDef gpio_init = {0};
    gpio_init.Pin = LL_GPIO_PIN_11 | LL_GPIO_PIN_12;
    gpio_init.Mode = LL_GPIO_MODE_ALTERNATE;
    gpio_init.OutputType = LL_GPIO_OUTPUT_PUSHPULL;
    gpio_init.Speed = LL_GPIO_SPEED_FREQ_VERY_HIGH;
    gpio_init.Pull = LL_GPIO_PULL_NO;
    gpio_init.Alternate = LL_GPIO_AF_10;
    LL_GPIO_Init(GPIOA, &gpio_init);

    // PA9 - VBUS Pin (Input, no pull)
    LL_GPIO_StructInit(&gpio_init);
    gpio_init.Pin = LL_GPIO_PIN_9;
    gpio_init.Mode = LL_GPIO_MODE_INPUT;
    gpio_init.Pull = LL_GPIO_PULL_NO;
    LL_GPIO_Init(GPIOA, &gpio_init);

    // PA10 - ID Pin
    LL_GPIO_StructInit(&gpio_init);
    gpio_init.Pin = LL_GPIO_PIN_10;
    gpio_init.Mode = LL_GPIO_MODE_ALTERNATE;
    gpio_init.OutputType = LL_GPIO_OUTPUT_OPENDRAIN;
    gpio_init.Pull = LL_GPIO_PULL_UP;
    gpio_init.Alternate = LL_GPIO_AF_10;
    gpio_init.Speed = LL_GPIO_SPEED_FREQ_VERY_HIGH;
    LL_GPIO_Init(GPIOA, &gpio_init);

    // Configure and enable USB OTG FS interrupt
    NVIC_SetPriority(OTG_FS_IRQn, 5);
    NVIC_EnableIRQ(OTG_FS_IRQn);
}

// Initialize onboard status LED (PC13)
void LED_Init(void) {
    LL_AHB1_GRP1_EnableClock(LL_AHB1_GRP1_PERIPH_GPIOC);

    LL_GPIO_InitTypeDef gpio_init = {0};
    gpio_init.Pin = LL_GPIO_PIN_13;
    gpio_init.OutputType = LL_GPIO_OUTPUT_PUSHPULL;
    gpio_init.Mode = LL_GPIO_MODE_OUTPUT;
    gpio_init.Speed = LL_GPIO_SPEED_FREQ_LOW;
    gpio_init.Pull = LL_GPIO_PULL_NO;
    LL_GPIO_Init(GPIOC, &gpio_init);
    LL_GPIO_SetOutputPin(GPIOC, LL_GPIO_PIN_13);
}


// ==========================================
// Main Function
// ==========================================
int main(void) {

    // 1. Hardware initialization
    GetEnablePRF_InstDataCache();
    SystemClock_Config();
    Systick_LL_Init();
    USB_LL_Init();
    LED_Init();

    // 2. RTOS Kernel initialization
    osKernelInitialize();

    NVIC_SetPriorityGrouping(3); // Configure priority grouping for RTX5

    // 3. Create task
    Task_Main_id = osThreadNew(Task_Main, NULL, &Task_Main_att);
    osKernelStart();

    while (1) {
    }
}