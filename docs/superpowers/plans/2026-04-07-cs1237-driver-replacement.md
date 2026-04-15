# CS1237 Driver Replacement Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the HX711 bit-banged ADC path with a CS1237 driver while keeping the existing PA7/PA5 wiring and main-loop pressure acquisition flow.

**Architecture:** Add a focused `cs1237` driver that owns the 2-wire timing, config register writes, gain selection, raw signed-code reading, tare capture, and simple weight conversion helpers. Update `main.c` to call the new driver with the same calibration constants and UART reporting pattern so the application behavior stays familiar.

**Tech Stack:** STM32F0 HAL, C, GPIO bit-banged serial timing, UART debug output

---

### Task 1: Add CS1237 Driver Interface

**Files:**
- Create: `Core/Inc/cs1237.h`

- [ ] Define the public macros and function prototypes for GPIO mapping, gain selection, speed selection, raw reads, tare, scaling, and weight conversion.
- [ ] Keep the API shape close to the existing `hx711` driver so `main.c` can be migrated with minimal edits.

### Task 2: Implement CS1237 Driver

**Files:**
- Create: `Core/Src/cs1237.c`
- Reference: `EIDE/tmp_cs1237_manual.pdf`

- [ ] Implement GPIO helpers for SCLK and bidirectional DOUT/DRDY handling.
- [ ] Implement a 24-bit signed ADC read using the manual's `24 + 3` clock sequence.
- [ ] Implement config register write/read support for PGA and output speed.
- [ ] Implement tare capture, median filtering, and weight conversion helpers using the current project’s scale-factor style.

### Task 3: Migrate Main Loop

**Files:**
- Modify: `Core/Src/main.c`

- [ ] Replace `hx711.h` include and setup calls with `cs1237.h`.
- [ ] Keep the existing calibration constants and UART reporting flow, but switch variable names and printed labels to `CS1237`.
- [ ] Continue doing power-on tare, periodic raw reads, net code calculation, and converted pressure/weight output.

### Task 4: Verify Build

**Files:**
- Verify: `EIDE` project build output

- [ ] Build the STM32 project from the local toolchain or available build command.
- [ ] Fix any compile or link errors caused by the driver replacement.
- [ ] Record any remaining assumptions if the hardware-dependent behavior cannot be fully exercised locally.
