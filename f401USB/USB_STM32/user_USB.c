#include "main.h"


// ==========================================
// Time-base functions required by TinyUSB
// ==========================================
uint32_t board_millis(void) {
    return GetSysTick();
}

uint32_t tusb_time_millis(void) {
    return GetSysTick();
}

uint32_t tusb_time_millis_api(void) {
    return GetSysTick();
}

// ==========================================
// USB OTG Interrupt Service Routines
// ==========================================
void OTG_FS_IRQHandler(void) {
    tud_int_handler(0);
}

void OTG_FS_WKUP_IRQHandler(void) {
    tud_int_handler(0);
}

// ==========================================
// TinyUSB HID Callbacks
// ==========================================

// Invoked when host requests GET_REPORT control transfer
__attribute__((used))
uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id,
                                hid_report_type_t report_type,
                                uint8_t* buffer, uint16_t reqlen) {
    (void) instance;
    (void) report_id;
    (void) report_type;
    (void) buffer;
    (void) reqlen;
    return 0;
}

// Invoked when host sends SET_REPORT control transfer (e.g. keyboard LEDs state)
__attribute__((used))
void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id,
                            hid_report_type_t report_type,
                            uint8_t const* buffer, uint16_t bufsize) {
    (void) instance;
    (void) report_id;
    (void) report_type;
    (void) buffer;
    (void) bufsize;
}

// ==========================================
// TinyUSB Device Callbacks
// ==========================================

// Invoked when device is mounted (configured)
void tud_mount_cb(void) {
}

// Invoked when device is unmounted
void tud_umount_cb(void) {
}

// Invoked when USB bus is suspended
void tud_suspend_cb(bool remote_wakeup_en) {
    (void) remote_wakeup_en;
}

// Invoked when USB bus is resumed
void tud_resume_cb(void) {
}