#ifndef REMOTE_ACTIONS_H
#define REMOTE_ACTIONS_H

#include <stdint.h>
#include "ir_nec.h"

// NEC command codes of the remote
#define LEFT_BTN         90
#define RIGHT_BTN        27
#define CENTER_BTN       26
#define UP_BTN           6
#define DOWN_BTN         22
#define MUTE_BTN_BELOW   13
#define POWER_BTN        89
#define POWER_BTN_BELOW  77
#define EXIT_BTN         5
#define REPEAT_BTN       18
#define PAGE_M_BTN       17

// Address filter: set REMOTE_ADDR to the address of your remote
#define REMOTE_ADDR_ANY  48896U
#define REMOTE_ADDR      REMOTE_ADDR_ANY

// Looks the command up in the keymap and runs its action (blocking).
// After a non-repeatable action (single press / combo) all IR events that
// arrived meanwhile are dropped.
void Remote_Process(NEC_Event_t evt, uint16_t addr, uint8_t cmd);

#endif