#include "remote_actions.h"
#include "cmsis_os2.h"
#include "tusb.h"
#include <stdbool.h>
#include <stddef.h>

/* ---------------------------- configuration ---------------------------- */

#define HID_INST_KEYBOARD       0
#define HID_INST_CONSUMER       2
#define HID_CONSUMER_REPORT_ID  0x01

#define HID_DEFAULT_HOLD_MS     50U    // key held time for a tap
#define HID_END_GAP_MS          50U    // pause after a whole action
#define HID_READY_TIMEOUT_MS    100U   // max wait for tud_hid_n_ready()

// Optional: protect TinyUSB calls if tud_task() runs in another thread
// and TinyUSB has no OSAL mutexes. Define these before including this file.
#ifndef HID_LOCK
#define HID_LOCK()
#define HID_UNLOCK()
#endif

// Consumer page usages
#define USAGE_VOLUME_UP     0xE9
#define USAGE_VOLUME_DOWN   0xEA
#define USAGE_MUTE          0xE2
#define USAGE_POWER         0x30

/* ------------------------------ data types ----------------------------- */

typedef enum { STEP_KEY, STEP_CONSUMER, STEP_WAIT } StepType_t;

typedef struct {
    uint8_t  type;      // StepType_t
    uint8_t  modifier;  // STEP_KEY: KEYBOARD_MODIFIER_*
    uint16_t code;      // STEP_KEY: HID keycode, STEP_CONSUMER: usage
    uint16_t ms;        // STEP_WAIT: delay, otherwise hold time (0 = default)
} Step_t;

typedef struct {
    uint8_t        ir_cmd;
    uint8_t        repeatable;  // 1 = repeat frames re-trigger the action
    const Step_t  *steps;
    uint8_t        count;
} KeyMap_t;

#define KEY(mod, code)            { STEP_KEY,      (mod), (code),  0 }
#define KEY_HOLD(mod, code, ms)   { STEP_KEY,      (mod), (code),  (ms) }
#define CONSUMER(usage)           { STEP_CONSUMER, 0,     (usage), 0 }
#define CONSUMER_HOLD(usage, ms)  { STEP_CONSUMER, 0,     (usage), (ms) }
#define WAIT(ms)                  { STEP_WAIT,     0,     0,       (ms) }

/* ------------------------------- actions ------------------------------- */

static const Step_t s_enter[]       = { KEY(0, HID_KEY_ENTER) };
static const Step_t s_space[]       = { KEY(0, HID_KEY_SPACE) };
static const Step_t s_arrow_right[] = { KEY(0, HID_KEY_ARROW_RIGHT) };
static const Step_t s_arrow_left[]  = { KEY(0, HID_KEY_ARROW_LEFT) };
static const Step_t s_alt_f4[]      = { KEY(KEYBOARD_MODIFIER_LEFTALT,  HID_KEY_F4) };
static const Step_t s_ctrl_w[]      = { KEY(KEYBOARD_MODIFIER_LEFTCTRL, HID_KEY_W) };
static const Step_t s_vol_up[]      = { CONSUMER(USAGE_VOLUME_UP) };
static const Step_t s_vol_down[]    = { CONSUMER(USAGE_VOLUME_DOWN) };
static const Step_t s_mute[]        = { CONSUMER(USAGE_MUTE) };
static const Step_t s_power[]       = { CONSUMER_HOLD(USAGE_POWER, 350) };

// F5 -> wait -> F -> wait -> SPACE
static const Step_t s_f5_combo[] = {
    KEY(0, HID_KEY_F5),
    WAIT(3500),
    KEY(0, HID_KEY_F),
    WAIT(2500),
    KEY(0, HID_KEY_SPACE),
};

/* -------------------------------- keymap -------------------------------- */

#define COUNT(a)           ((uint8_t)(sizeof(a) / sizeof((a)[0])))
#define MAP(cmd, rep, arr) { (cmd), (rep), (arr), COUNT(arr) }

static const KeyMap_t keymap[] = {
    MAP(CENTER_BTN,      0, s_enter),
    MAP(RIGHT_BTN,       1, s_vol_up),
    MAP(LEFT_BTN,        1, s_vol_down),
    MAP(MUTE_BTN_BELOW,  0, s_mute),
    MAP(UP_BTN,          1, s_arrow_right),
    MAP(DOWN_BTN,        1, s_arrow_left),
    MAP(POWER_BTN,       0, s_power),
    MAP(POWER_BTN_BELOW, 0, s_alt_f4),
    MAP(EXIT_BTN,        0, s_ctrl_w),
    MAP(REPEAT_BTN,      0, s_f5_combo),
    MAP(PAGE_M_BTN,      0, s_space),
};

/* ------------------------------- executor ------------------------------- */

static bool hid_wait_ready(uint8_t inst)
{
    for (uint32_t t = 0; t < HID_READY_TIMEOUT_MS; t++) {
        if (tud_hid_n_ready(inst)) return true;
        osDelay(1);
    }
    return false;
}

static void hid_send(const Step_t *s, bool press)
{
    HID_LOCK();

    if (s->type == STEP_KEY) {
        if (press) {
            uint8_t keycode[6] = { (uint8_t)s->code };
            tud_hid_keyboard_report(0, s->modifier, keycode);
        } else {
            tud_hid_keyboard_report(0, 0, NULL);
        }
    } else {
        uint8_t report[3] = {
            HID_CONSUMER_REPORT_ID,
            press ? (uint8_t)(s->code & 0xFF) : 0,
            press ? (uint8_t)(s->code >> 8)   : 0
        };
        tud_hid_n_report(HID_INST_CONSUMER, 0, report, sizeof(report));
    }

    HID_UNLOCK();
}

// press -> hold -> release
static bool hid_tap(const Step_t *s)
{
    uint16_t hold = s->ms ? s->ms : HID_DEFAULT_HOLD_MS;
    uint8_t  inst = (s->type == STEP_KEY) ? HID_INST_KEYBOARD : HID_INST_CONSUMER;

    if (!hid_wait_ready(inst)) return false;   // nothing sent, nothing to release
    hid_send(s, true);
    osDelay(hold);

    if (!hid_wait_ready(inst)) return false;   // USB is gone: the host drops the key state itself
    hid_send(s, false);
    return true;
}

static void run_steps(const Step_t *steps, uint8_t count)
{
    for (uint8_t i = 0; i < count; i++) {
        if (steps[i].type == STEP_WAIT) {
            osDelay(steps[i].ms);
        } else if (!hid_tap(&steps[i])) {
            break;                              // abort the sequence if USB is not ready
        }
    }
    osDelay(HID_END_GAP_MS);
}

void Remote_Process(NEC_Event_t evt, uint16_t addr, uint8_t cmd)
{
#if REMOTE_ADDR != REMOTE_ADDR_ANY
    if (addr != REMOTE_ADDR) return;
#else
    (void)addr;
#endif

    for (uint8_t i = 0; i < COUNT(keymap); i++) {

        const KeyMap_t *m = &keymap[i];
        if (m->ir_cmd != cmd) continue;

        if (evt == NEC_EVT_REPEAT && !m->repeatable) return;

        run_steps(m->steps, m->count);

        // single press / combo: drop everything that arrived while it was running
        if (!m->repeatable) NEC_Flush();
        return;
    }
}