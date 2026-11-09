# IR Remote → USB HID Keyboard

STM32 firmware for controlling a PC with an NEC infrared remote via USB HID. Remote commands are translated into keyboard key presses, media controls, or key sequences.

The project demonstrates low-level driver development, RTOS integration, and USB device stack implementation in a complete hardware-software project.

## What it does

- Decodes the **NEC IR protocol** (standard and extended addressing, repeat frames for held buttons).
- Presents itself to the PC as a **composite USB HID device** (keyboard + consumer control).
- Maps each remote button to an action through a **table-driven keymap**:
  - single keys and shortcuts (Enter, arrows, Alt+F4, Ctrl+W, Space)
  - media keys (volume up/down, mute, power)
  - timed **key sequences** (e.g. `F5 → wait → F → wait → Space`)
  - held buttons auto-repeat only where it makes sense (volume, arrows)

## Stack

| Layer | Details |
|---|---|
| Drivers | Register-level / LL (no HAL for the IR path) |
| IR capture | TIM3 channel 3 input capture on PB0, hardware input filter, falling-edge timing |
| RTOS | Keil RTX5 (CMSIS-RTOS2), custom TinyUSB OSAL on top of it |
| USB | TinyUSB device stack, HID keyboard and consumer-control interfaces |

## Toolchain & Environment

* **Environment:** VS Code + Keil Studio Pack
* **Toolchain:** Keil MDK v6 (Arm Compiler 6)
* **Build System:** CMSIS-Toolbox (`csolution` / `cbuild`)
* **Target MCU:** STM32F401RCT6 (LQFP64, 256 KB Flash, 64 KB RAM)

Footprint: ~18 KB code, ~1.4 KB RO data (including RTOS and USB stack).

## How it works

```
IR receiver ─► TIM3 input capture ISR ─► NEC state machine ─► thread flag
                                                                   │
       USB host ◄─ TinyUSB HID reports ◄─ action executor ◄─ keymap lookup
```

- **`ir_nec.c/h`** – interrupt-driven decoder. Measures the interval between falling edges, validates the frame (command/inverted-command check), recovers from noise by treating a bad edge as a possible new leader, and reports `NEW` / `REPEAT` events. No polling: the ISR wakes the task through a callback.
- **`remote_actions.c/h`** – the keymap and the action executor. Actions are arrays of steps (`KEY`, `CONSUMER`, `WAIT`), so adding a button is one table row, not new code. USB readiness is checked with timeouts, so nothing hangs if the host disconnects.
- **`rtos.c`** – tasks: USB stack task and the IR/HID task.
- **`usb_descriptors.c`, `tusb_config.h`** – composite HID descriptors and TinyUSB configuration.

## Adding a button

```c
static const Step_t s_mute[] = { CONSUMER(USAGE_MUTE) };
...
MAP(MUTE_BTN_BELOW, 0, s_mute),   // IR command, repeatable?, action
```

## Status

Working on hardware: all buttons, key sequences, media keys and hold-to-repeat verified on a PC.