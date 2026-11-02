#include "rtos.h"

#define NEC_TEST 


//for test NEC recive 
#ifdef NEC_TEST
__IO uint16_t    NEC_adress_recive;
__IO uint8_t     NEC_code_recive;
__IO uint8_t     NEC_repeat_cnt;
__IO NEC_Event_t NEC_last_evt;
__IO uint32_t    NEC_new_cnt;         // number of new frames
__IO uint32_t    NEC_repeat_evt_cnt;  // number of repeat events
#endif



// ==========================================
// 1. USB Stack Task (TinyUSB Task)
// ==========================================
osThreadId_t Task_Usb_id;
const osThreadAttr_t Task_Usb_att = {
    .name = "Task_Usb",
    .stack_size = 2048,
    .priority = osPriorityAboveNormal
};

void Task_Usb(void *argument) {
    (void) argument;

    // Initialize TinyUSB stack as device
    if (!tud_init(BOARD_TUD_RHPORT)) {
        while (1); // Initialization failed
    }

    osDelay(10);

    // Force disable VBUS sensing (required for boards where PA9 is disconnected)
    USB_OTG_FS->GCCFG |= (1UL << 21); // NOVBUSSENS

    while (1) {
        tud_task();      // Process USB stack events
        osThreadYield(); // Yield CPU time if tud_task is non-blocking
    }
}

// ==========================================
// 2. Main Application Task (HID Keyboard)
// ==========================================
osThreadId_t Task_Main_id;
const osThreadAttr_t Task_Main_att = {
    .name = "Task_Main",
    .stack_size = 2048,
    .priority = osPriorityNormal
};

void Task_Main(void *argument) {
    (void) argument;

    // Spawn the USB background task
    Task_Usb_id = osThreadNew(Task_Usb, NULL, &Task_Usb_att);

    // Wait until the device is enumerated and configured by the host
    while (!tud_mounted()) {
        osDelay(100);
    }

    while (1) {
        // Check if HID endpoint is ready to accept a report
        if (tud_hid_ready()) {
            uint8_t keycode[6] = {0};
            keycode[0] = HID_KEY_S; // Key 'S'

            // Send key press report
            tud_hid_keyboard_report(0, 0, keycode);

            // Hold key down for 50 ms
            osDelay(50);

            // Send key release report (empty report)
            tud_hid_keyboard_report(0, 0, NULL);

            // Sleep the remaining 950 ms to complete a 1-second cycle
            osDelay(950);
        } else {
            // Wait briefly before retrying if endpoint was not ready
            osDelay(10);
        }
    }
}


// ==========================================
// 3. IR NEC TASK
// ==========================================
osThreadId_t Task_NEC_id;
const osThreadAttr_t Task_NEC_att = {
    .name = "Task_NEC",
    .priority = osPriorityNormal,
    .stack_size = 512
};

#ifdef NEC_TEST
void Task_NEC(void *argument){
   (void) argument;

    NEC_IR_CaptureInit(); 

    while(1){

        uint16_t addr;  
        uint8_t  cmd;
        NEC_Event_t evt = NEC_Read(&addr, &cmd);

        if (evt != NEC_EVT_NONE) {
            NEC_adress_recive = addr;
            NEC_code_recive   = cmd;
            NEC_last_evt      = evt;
            NEC_repeat_cnt    = NEC_GetRepeatCount();

            if (evt == NEC_EVT_NEW) NEC_new_cnt++;
            else                    NEC_repeat_evt_cnt++;
        }

        osDelay(10);       
    }
}
#else
void Task_NEC(void *argument){
   (void) argument;

    NEC_IR_CaptureInit(); 

    while(1){

        uint16_t addr;  
        uint8_t  cmd;
        NEC_Event_t evt = NEC_Read(&addr, &cmd);

        if (evt != NEC_EVT_NONE) {
            NEC_adress_recive = addr;
            NEC_code_recive   = cmd;
            NEC_last_evt      = evt;
            NEC_repeat_cnt    = NEC_GetRepeatCount();

            if (evt == NEC_EVT_NEW) NEC_new_cnt++;
            else                    NEC_repeat_evt_cnt++;
        }

        osDelay(10);       
    }
}


#endif







// ==========================================
// RTX5 Idle Thread
// ==========================================
uint32_t idleCount = 0;

void osRtxIdleThread(void *argument) {
    (void) argument;
    for (;;) {
        idleCount++;
    }
}














