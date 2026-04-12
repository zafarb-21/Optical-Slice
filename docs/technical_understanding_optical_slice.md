# Technical Understanding Of Our Project Portion: Optical Slice

This document is a technical breakdown of the optical slice portion of the project as it exists in this repo today. It focuses on the firmware architecture, the libraries we rely on, the hardware layout, the current packet / connection path, and how interrupts are actually being used.

## 1. What The Optical Slice Is

The optical slice is the STM32-based sensing node in this project. Its job is to collect optical and environmental observations from the local sensor stack, combine them into one software frame, and expose that data to the rest of the system.

Right now, the optical slice firmware is built around this topology:

- `STM32F303K8T6` on `NUCLEO-F303K8`
- `SPI1` from STM32 to `SC18IS604`
- downstream sensor-side I2C behind the `SC18IS604`
- direct STM32 GPIO for the laser transmitter / receiver path
- reserved native `I2C1` link for the upstream master board
- `USART2` for bring-up and live debug output

The key architectural decision is that local optical sensors do not sit directly on the STM32 `I2C1` bus. Instead, the firmware talks to the `SC18IS604` over `SPI1`, and the bridge performs downstream I2C transactions for the sensor bus. The STM32 `I2C1` peripheral is being kept for the future master-board connection.

## 2. Libraries And Firmware Components We Use

### STM32 platform stack

The firmware uses the standard STM32Cube-generated base:

- `STM32CubeIDE` project generation
- `STM32F3xx HAL` drivers under `Drivers/STM32F3xx_HAL_Driver/`
- `CMSIS` headers under `Drivers/CMSIS/`

These libraries are responsible for:

- GPIO setup
- SPI setup
- I2C setup
- UART setup
- SysTick time base through `HAL_GetTick()`
- EXTI dispatch through `HAL_GPIO_EXTI_IRQHandler()`

### Sensor and bridge libraries

The project also uses ST's official `VL53L1X` ULD package:

- `STSW-IMG009/STSW-IMG009_v3.5.5`

Relevant local integration files are:

- `Core/Src/vl53l1x_bridge.c`
- `Core/Src/vl53l1_platform.c`
- `Core/Src/VL53L1X_api.c`
- `Core/Src/VL53L1X_calibration.c`

This stack is used to:

- boot-check the `VL53L1X`
- initialize the sensor
- configure distance mode / timing budget / inter-measurement timing
- poll data-ready
- read range status and distance
- clear measurement interrupts

### Project-specific modules

The custom project logic is split mainly across these files:

- `Core/Src/optical_slice_app.c`
- `Core/Src/optical_slice_sensors.c`
- `Core/Src/sc18is604.c`
- `Core/Src/optical_slice_validation.c`

Responsibilities:

- `optical_slice_app.c`
  Handles top-level initialization, live reporting, event-based reporting rules, and the `SC18_INT` callback handoff.
- `optical_slice_sensors.c`
  Handles bridge-backed sensor startup and polling for `BH1750`, `VL53L1X`, `WonderCam`, and the laser GPIO path.
- `sc18is604.c`
  Implements the SPI-to-I2C bridge driver and blocking bridge transactions.
- `optical_slice_validation.c`
  Provides a snapshot-based validation helper for bench bring-up.

## 3. Hardware Layout

### MCU and board

- MCU: `STM32F303K8T6`
- board target: `NUCLEO-F303K8`
- system clock source in current config: `HSI`
- current system clock: `8 MHz`

### Main hardware partition

The optical slice hardware is organized into three communication regions:

1. STM32 local control region
   The STM32 directly owns reset, chip-select, interrupt sensing, laser TX, laser RX, and UART debug.
2. Bridge region
   The `SC18IS604` converts STM32 `SPI1` traffic into downstream I2C transactions.
3. Sensor region
   `BH1750`, `VL53L1X`, and `WonderCam` sit behind the bridge on the downstream I2C side.

### Pin-level layout

- `PB3` -> `SPI1_SCK`
- `PB4` -> `SPI1_MISO`
- `PB5` -> `SPI1_MOSI`
- `PA4` -> `SC18_CS`
- `PA8` -> `SC18_RESET`
- `PA9` -> `SC18_INT`
- `PB0` -> `LASER_TX`
- `PA12` -> `LASER_RX`
- `PB6` -> `I2C1_SCL` to upstream master board
- `PB7` -> `I2C1_SDA` to upstream master board
- `PA2` -> `USART2_TX`
- `PA15` -> `USART2_RX`

### Known downstream sensor addresses

- `BH1750` default address: `0x23`
- `BH1750` alternate address: `0x5C`
- `VL53L1X` default address: `0x29`
- `WonderCam` default address: `0x32`

## 4. Packet Structure / Connection Path

This section describes how information moves through the optical slice. We now have a working connection path, an internal software frame, and an implemented upstream `I2C1` slave packet interface for status, configuration, and diagnostics.

### A. Sensor communication path

The data path for local optical sensors is:

`STM32 -> SPI1 -> SC18IS604 -> downstream I2C -> sensor`

This is the active path for:

- `BH1750`
- `VL53L1X`
- `WonderCam`

The laser path is separate:

`STM32 GPIO -> laser TX / laser RX`

This means the laser front end is not packetized over I2C at all. It is read and driven directly through GPIO logic in the MCU.

### B. Internal software frame

The main internal data container is `optical_slice_frame_t` in `Core/Inc/optical_slice_types.h`.

The frame currently carries:

- `timestamp_ms`
- `status_flags`
- `ambient_lux_x10`
- `object_distance_mm`
- `snow_height_mm`
- `precipitation_level_x10`
- `precipitation_type`
- `motion_detected`
- `package_detected`
- `presence_detected`
- `dark_detected`
- `camera_online`
- `laser_online`
- `laser_signal_detected`
- `tof_range_status`

This is effectively the current in-memory packet for the optical slice application.

### C. Runtime reporting path

The application publishes a human-readable ASCII report over `USART2`, and it also maintains a packetized upstream interface on `I2C1`.

Current runtime output includes:

- boot banner
- boot diagnostics
- bridge version text
- `VL53PROBE` diagnostics
- `LIVE` status reports

The `LIVE` report summarizes:

- ambient light
- dark state
- `VL53L1X` distance
- coarse height level derived from distance
- laser signal status
- package detection status
- precipitation classification
- camera online / offline status

This UART path is for bring-up and debugging. The master-board-facing exported packet path now lives on `I2C1`.

### D. Upstream master-board connection status

`I2C1` is configured as the master-board-facing slave interface and now serves packetized state to the upstream controller.

At the current stage:

- the STM32 listens at `0x42` as a 7-bit slave
- the upstream side can read a status packet, a configuration packet, or a diagnostics packet
- the upstream side can issue single-byte commands to select packets, capture or clear the snow baseline, choose a laser timing profile, and reset diagnostics

## 5. Bus Configuration Details

### SPI1

The `SC18IS604` bridge is driven from `SPI1` with these settings:

- mode: master
- direction: 2-line
- data size: 8-bit
- polarity: high
- phase: second edge
- software NSS
- prescaler: `SPI_BAUDRATEPRESCALER_8`

The generated project comment shows an effective SPI rate of about `1000 KBits/s`.

The bridge driver also inserts a small inter-byte delay:

- `SC18_SPI_INTERBYTE_DELAY_US = 10`

### Downstream I2C through the bridge

The downstream sensor bus is not handled by the STM32 I2C peripheral. Instead, the STM32 sends SPI commands to the `SC18IS604`, and the bridge performs:

- I2C write
- I2C read
- I2C write-read

Important project note:

The `VL53L1X` path does not reliably work with the bridge's combined read-after-write behavior on this hardware. The firmware therefore uses split transactions for `VL53L1X` register reads:

1. write the 16-bit register address
2. perform a separate read transaction

### Native I2C1

`I2C1` is configured on:

- `PB6` = `SCL`
- `PB7` = `SDA`

Current configuration:

- 7-bit addressing
- own address = `0`
- analog filter enabled
- digital filter disabled

Right now, this interface is configured at the HAL level but not yet used as the final application protocol path.

### USART2

Debug output is sent over `USART2` using:

- baud rate: `38400`
- word length: `8`
- parity: none
- stop bits: `1`

The current transmit path is blocking `HAL_UART_Transmit()`.

## 6. Sensor-Specific Behavior

### BH1750

`BH1750` is used for ambient light sensing. The code places it into continuous high-resolution mode and periodically reads the two-byte measurement through the bridge.

The result is stored as:

- `ambient_lux_x10`

The dark threshold is currently:

- `OPTICAL_BH1750_DARK_THRESHOLD_X10 = 100`

So the scene is treated as dark at `10.0 lx` or below.

### VL53L1X

`VL53L1X` is the distance sensor used for object / height measurement. The bridge layer initializes the sensor and then polls it for data-ready.

Current default configuration in the bridge layer:

- distance mode: `2`
- timing budget: `100 ms`
- inter-measurement period: `100 ms`

The measured value is stored as:

- `object_distance_mm`

The application also derives a coarse state from that distance:

- `< 100 mm` -> alert
- `< 300 mm` -> tracking
- otherwise -> clear

The snow-height conversion itself now exists in firmware. It uses a calibrated baseline minus measured distance, with the baseline held as runtime configuration so the installed geometry can be captured during bring-up.

### WonderCam

`WonderCam` is currently treated as a classification sensor behind the bridge. The code:

- reads a firmware register to confirm presence
- switches the module into classification mode
- enables the LED
- reads the summary block

The current class mapping in firmware is:

- class `1` -> none
- class `5` -> ice
- class `10` -> package
- class `12` -> snow

Confidence and stability filtering are also applied before updating the cached result.

This path is implemented in code, but it should still be considered integration in progress until fully bench-validated.

### Laser path

The laser path is not behind the bridge and not behind I2C.

- `PB0` drives the transmitter
- `PA12` reads the receiver

The receiver is currently handled as a digital input with pull-up enabled. The application exposes:

- raw laser signal presence
- laser online state

At this stage, the laser-driven higher-level fields are implemented in the main frame. `presence_detected` and `motion_detected` are derived from beam state transitions and timing windows, and the timing windows are now selectable through runtime laser profiles.

## 7. IRQ Utilization

Interrupt usage in the current optical slice firmware is intentionally light. Most sensor communication is still polling-based.

### SysTick

`SysTick_Handler()` calls `HAL_IncTick()`.

This is the timing base for:

- `HAL_GetTick()`
- sensor poll spacing
- bridge timeout loops
- UART report debounce timing
- boot / initialization delays

This is the most actively used interrupt-backed timing source in the project.

### EXTI for `SC18_INT`

`PA9` is configured as:

- `GPIO_MODE_IT_RISING_FALLING`

`EXTI9_5_IRQHandler()` calls:

- `HAL_GPIO_EXTI_IRQHandler(SC18_INT_Pin)`

Then the application-level callback in `optical_slice_app.c` handles it:

- `HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin)`

Current behavior:

- if the interrupt came from `SC18_INT_Pin`, the code sets `sc18_irq_pending = 1`
- the main app loop later consumes that flag
- the flag is only used as an event hint for reporting, especially around `WonderCam` updates

Important detail:

The interrupt is not currently used to drive a full asynchronous bridge transaction engine. It only marks that bridge-related activity happened, and the main loop still performs the actual sensor polling work.

### I2C1 event IRQ

`I2C1_EV_IRQHandler()` is enabled and forwards to:

- `HAL_I2C_EV_IRQHandler(&hi2c1)`

In the current optical slice application:

- the event IRQ feeds the HAL slave state machine for upstream `I2C1` communication
- the optical slice code attaches packet-selection and command handling to that slave path
- the complementary error IRQ is also enabled so the listen state can be recovered after bus faults

So this interrupt path is now part of the real optical slice data-export path.

### What Is Not Using IRQs Right Now

The following paths are still effectively blocking / polling based:

- `SPI1` transfers to the `SC18IS604`
- downstream bridge-backed I2C sensor transactions
- UART debug transmission
- `BH1750` sampling
- `VL53L1X` readiness polling
- most application-level state evaluation

So the current design is best described as:

- interrupt-assisted state notification
- polling-driven sensor communication

## 8. Current Technical Limitations

From a systems point of view, these are the main limitations still present:

- the `WonderCam` path needs more real-hardware validation
- the UART stream is still a debug interface, not the final exported packet layer
- the bridge transaction model is blocking rather than DMA- or IRQ-driven
- the runtime baseline still has to be captured on the installed hardware before snow height leaves the `uncalibrated` state
- laser timing profiles still need final selection on the installed hardware after beam alignment and environmental testing

## 9. Practical Summary

The optical slice firmware already has a clear technical foundation:

- STM32 controls the system
- `SPI1` drives the `SC18IS604`
- the bridge talks to downstream sensors
- GPIO handles the laser path
- `USART2` provides live visibility during bring-up

The current implementation is strong on hardware bring-up, sensor integration, and upstream data export. The remaining work is mainly calibration, bench validation, and long-duration reliability tuning.
