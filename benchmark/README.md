# zephyr-stm32n6-ai-people-detection Application

Zephyr Computer Vision application demonstrating the deployment of object detection models on the [STM32N6570-DK](https://docs.zephyrproject.org/latest/boards/st/stm32n6570_dk/doc/index.html).

---

## Features Demonstrated in This Example

- NPU-accelerated quantized AI model inference under Zephyr on STM32N6570-DK

---

# Install Application

The following commands show how to install and build this application, assuming that the [Zephyr SDK](https://docs.zephyrproject.org/latest/develop/toolchains/zephyr_sdk.html) and
[host tools](https://docs.zephyrproject.org/latest/develop/getting_started/index.html#install-dependencies) are installed:


```bash
$ mkdir workspace
$ cd workspace
workspace $ west init -m https://github.com/stm32-hotspot/zephyr-stm32n6-ai-people-detection
workspace $ west update
```

---

# Build application

## Build application for development

```bash
workspace $ cd zephyr-stm32n6-ai-people-detection
workspace/zephyr-stm32n6-ai-people-detection $ west build app -p
```

## Build application to boot from flash

```bash
workspace $ cd zephyr-stm32n6-ai-people-detection
workspace/zephyr-stm32n6-ai-people-detection $ west build -b stm32n6570_dk  --sysbuild app -DSB_CONFIG_MCUBOOT_MODE_RAM_LOAD=y -p
```

---

# Flash application model weights

This needs to be done once (unless you update the model).
Set your board to [development mode](#boot-modes).
Ensure **no** `ST-LINK_gdbserver` is running

```bash
workspace/zephyr-stm32n6-ai-people-detection $ west flash-weights
```

---

# Execute application

## When application is built for development

Set your board to [development mode](#boot-modes).

```bash
workspace/zephyr-stm32n6-ai-people-detection $ west debug
...
Transfer rate: 698 KB/sec, 6890 bytes/write.
(gdb) c
Continuing.
```

You should see the following output on the console

```
[00:00:00.341,000] <inf> flash_stm32_xspi: XSPI flash config is OCTO / DTR
[00:00:00.341,000] <inf> flash_stm32_xspi: Read SFDP from externalFlash
[00:00:00.341,000] <inf> flash_stm32_xspi: Read SFDP from externalFlash
[00:00:00.341,000] <inf> flash_stm32_xspi: Memory-mapped NOR-flash at 0x70000000 (0x8000000 bytes)
evision ST-AE v1.0.5
evision AWB v1.0.5
evision ST-AE v1.0.5
evision AWB v1.0.5
evision ST-AE v1.0.5
evision AWB v1.0.5
*** Booting Zephyr OS build v4.2.0 ***
[00:00:00.462,000] <inf> model: Npu subsystem ready
[00:00:00.466,000] <inf> main: buffer 0x3405c198(0x900bb910) queue to endpoint@1 pipe
[00:00:00.466,000] <inf> main: buffer 0x3405c1b8(0x90177120) queue to endpoint@1 pipe
[00:00:00.466,000] <inf> main: buffer 0x3405c1d8(0x90232940) queue to endpoint@2 pipe
[00:00:00.466,000] <inf> main: buffer 0x3405c1f8(0x90257560) queue to endpoint@2 pipe
[00:00:00.466,000] <inf> main: Starting main pipe
[00:00:00.471,000] <inf> main: Starting aux pipe
[00:00:00.475,000] <inf> main: STARTING
```

## When application is built to boot from flash

### Flash MCUboot and application

Set your board to [development mode](#boot-modes).
Ensure **no** `ST-LINK_gdbserver` is running.

```bash
workspace/zephyr-stm32n6-ai-people-detection $ west flash
```

### Start MCUboot and application

Set your board to [boot from flash](#boot-modes). Then perform a power off/on sequence.

You should see the following output on the console

```
*** Booting MCUboot v2.1.0-rc1-389-g4eba8087fa60 ***
*** Using Zephyr OS build v4.2.0 ***
I: Starting bootloader
I: Primary slot: version=0.0.0+0
I: Image 0 Secondary slot: Image not found
I: Image 0 RAM loading to 0x34000000 is succeeded.
I: Image 0 loaded from the primary slot
I: Bootloader chainload address offset: 0x34000000
I: Image version: v0.0.0
I: Jumping to the first image slot
[00:00:00.201,000] <err> flash_stm32_xspi: NOR init'd in MemMapped mode
evision ST-AE v1.0.5
evision AWB v1.0.5
evision ST-AE v1.0.5
evision AWB v1.0.5
evision ST-AE v1.0.5
evision AWB v1.0.5
*** Booting Zephyr OS build v4.2.0 ***
[00:00:00.322,000] <inf> model: Npu subsystem ready
[00:00:00.326,000] <inf> main: buffer 0x3405c4f8(0x900bb910) queue to endpoint@1 pipe
[00:00:00.326,000] <inf> main: buffer 0x3405c518(0x90177120) queue to endpoint@1 pipe
[00:00:00.326,000] <inf> main: buffer 0x3405c538(0x90232940) queue to endpoint@2 pipe
[00:00:00.326,000] <inf> main: buffer 0x3405c558(0x90257560) queue to endpoint@2 pipe
[00:00:00.326,000] <inf> main: Starting main pipe
[00:00:00.331,000] <inf> main: Starting aux pipe
[00:00:00.335,000] <inf> main: STARTING
```

---

# Boot Modes

The STM32N6 series does not have internal flash memory. To retain firmware after a reboot, program it into the external flash. Alternatively, you can load firmware directly
into SRAM (development mode), but note that the program will be lost if the board is powered off in this mode.

**Development Mode:** Used for loading firmware into RAM during a debug session or for programming firmware into external flash.

**Boot from Flash:** Used to boot firmware from external flash.

|                  |                                                                              |
| -------------    | -------------                                                                |
| Boot from flash  | ![STM32N6570-DK Boot from flash](_htmresc/STM32N6570-DK_Boot_from_flash.png) |
| Development mode | ![STM32N6570-DK Development mode](_htmresc/STM32N6570-DK_Dev_mode.png)       |
