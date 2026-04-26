# STM32N6 Samples for Zephyr RTOS

This repository contains sample applications written in [Zephyr RTOS](https://zephyrproject.org/)
for the **STM32N6** microcontroller family, targeting the
[STM32N6570-DK](https://www.st.com/en/evaluation-tools/stm32n6570-dk.html) Discovery Kit board.

## Board: STM32N6570-DK

The STM32N6570-DK is a Discovery Kit based on the STM32N657X0H3Q microcontroller.
It features:

- STM32N657X0H3Q Arm® Cortex®-M55 core running at up to 800 MHz
- 1 Mbit of on-chip SRAM, 4 Mbits of OTP
- External 32 Mbit PSRAM and 1 Gbit XSPI NOR Flash
- USB Type-C, CAN FD, Ethernet, CSI camera connector
- LCD display with touch controller
- 2 user LEDs (green LD1, red LD2) and 1 user push-button
- Arduino Uno R3 expansion connector
- Onboard STLINK-V3EC debugger/programmer

The board identifier in Zephyr is: `stm32n6570_dk`

> **Note:** STM32N6 uses a First Stage Boot Loader (FSBL) approach. All samples
> must be built with `--sysbuild` flag to include the FSBL image alongside the
> application.

## Prerequisites

- [Zephyr SDK](https://docs.zephyrproject.org/latest/develop/getting_started/index.html)
  (version 0.17.0 or later)
- [west](https://docs.zephyrproject.org/latest/develop/west/index.html) build tool
- ARM GNU Toolchain

## Getting Started

1. Set up a Zephyr workspace (if not already done):

   ```bash
   west init zephyrproject
   cd zephyrproject
   west update
   ```

2. Clone this repository into your workspace or reference a sample directly.

3. Build and flash a sample using `west` with `--sysbuild`:

   ```bash
   west build -b stm32n6570_dk --sysbuild <path-to-sample>
   west flash
   ```

4. Open a serial terminal at **115200 baud** to view console output.

## Samples

### [hello_world](samples/hello_world/)

A simple Hello World application that prints a greeting message to the UART
console. This is the ideal starting point to verify your toolchain and board
setup.

**Key features demonstrated:**
- Basic Zephyr application structure
- UART console output via `printf`

```bash
west build -b stm32n6570_dk --sysbuild samples/hello_world
west flash
```

Expected output:
```
Hello World! stm32n6570_dk
```

---

### [blinky](samples/blinky/)

Blinks the green LED (LD1) on the STM32N6570-DK board once per second using
the Zephyr GPIO API.

**Key features demonstrated:**
- GPIO output configuration using devicetree aliases
- Periodic task using `k_msleep()`

```bash
west build -b stm32n6570_dk --sysbuild samples/blinky
west flash
```

The green LED (LD1) will blink with a 1-second period.

---

### [button](samples/button/)

Demonstrates GPIO input with interrupts. Prints a message to the console
whenever the user button is pressed, and lights the green LED (LD1) while
the button is held down.

**Key features demonstrated:**
- GPIO input configuration using devicetree aliases
- GPIO interrupt callback
- Reading GPIO pin state in a loop

```bash
west build -b stm32n6570_dk --sysbuild samples/button
west flash
```

Expected output:
```
Set up button at GPIOC pin 13
Set up LED at GPIOO pin 1
Press the button
Button pressed at 12345678
```

---

## Project Structure

```
samples/
├── hello_world/        # Basic Hello World over UART
│   ├── CMakeLists.txt
│   ├── prj.conf
│   ├── README.rst
│   └── src/
│       └── main.c
├── blinky/             # LED blink using GPIO
│   ├── CMakeLists.txt
│   ├── prj.conf
│   ├── README.rst
│   └── src/
│       └── main.c
└── button/             # Button input with GPIO interrupt
    ├── CMakeLists.txt
    ├── prj.conf
    ├── README.rst
    └── src/
        └── main.c
```

## License

SPDX-License-Identifier: Apache-2.0