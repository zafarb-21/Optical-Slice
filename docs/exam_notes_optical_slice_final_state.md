# Optical Slice Exam Notes: Current Finished System

This note is the exam-prep version of our optical slice based on what the firmware actually does now.

Important clarification:

- this document is written as the clean "finished system" explanation we would give in an exam
- the architecture and packet behavior below now match the current repo much more closely than the earlier draft
- the remaining uncertainty is mostly physical validation, especially final `WonderCam` behavior and threshold confirmation on installed hardware, not missing core firmware architecture

## 1. One-Paragraph Exam Answer

Our optical slice is an `STM32F303K8T6` sensing node that measures ambient light, time-of-flight distance, laser beam state, and camera-based classification. The STM32 talks to local optical sensors through an `SC18IS604` SPI-to-I2C bridge, while the laser transmitter and receiver are handled directly through GPIO. The firmware fuses all sensor readings into one application frame, interprets ToF distance as clear, snow, or obstruction, derives snow height, detects presence and motion from the laser path, classifies precipitation and package state with `WonderCam`, and exposes the resulting data to the master board over the STM32 native `I2C1` interface. `SysTick` provides timing, `SC18_INT` provides bridge event notification, and the `I2C1` interrupt path handles the upstream slave protocol.

## 2. Hardware Layout

### Controller

- MCU: `STM32F303K8T6`
- board: `NUCLEO-F303K8`

### Bus partition

The optical slice is split into three layers:

1. local MCU control layer
   The STM32 controls reset, chip select, interrupt input, laser TX, laser RX, and debug UART.
2. bridge transport layer
   The STM32 uses `SPI1` to communicate with the `SC18IS604`.
3. downstream sensor layer
   The `SC18IS604` drives the sensor-side I2C bus for local optical sensors.

### Signal ownership

- `SPI1` is used for `SC18IS604`
- downstream bridge-backed I2C is used for:
  - `BH1750`
  - `VL53L1X`
  - `WonderCam`
- direct GPIO is used for:
  - laser transmitter on `PB0`
  - laser receiver on `PA12`
- upstream `I2C1` is used for the master board on:
  - `PB6` = `I2C1_SCL`
  - `PB7` = `I2C1_SDA`
- `USART2` is used for debugging and bench visibility

### Sensor addresses

- `BH1750` = `0x23`
- `BH1750` alternate = `0x5C`
- `VL53L1X` = `0x29`
- `WonderCam` = `0x32`
- optical slice upstream `I2C1` slave address = `0x42`

## 3. Libraries And Firmware Modules

### Platform libraries

- `STM32CubeIDE` generated project
- `STM32F3xx HAL`
- `CMSIS`

These provide:

- GPIO
- SPI
- I2C peripheral control
- UART
- EXTI dispatch
- SysTick timing

### Sensor library

- ST `VL53L1X` ULD package from `STSW-IMG009`

It is used to:

- identify the `VL53L1X`
- initialize it
- configure and start ranging
- poll for ready data
- read distance and range status
- clear interrupts after each reading

### Project modules

- `optical_slice_app`
- `optical_slice_sensors`
- `sc18is604`
- `vl53l1x_bridge`
- `optical_slice_validation`
- `i2c`

In the current finished explanation:

- `optical_slice_sensors` owns acquisition, freshness checks, derived sensor state, and runtime tuning data
- `optical_slice_app` owns reporting and app-level state publication
- `sc18is604` owns bridge communication
- `vl53l1x_bridge` owns safe `VL53L1X` access through the bridge
- `optical_slice_validation` owns bench snapshot formatting
- `i2c` owns the upstream `I2C1` slave packet interface

## 4. Final Functional Behavior

### BH1750 role

`BH1750` measures ambient light.

Purpose:

- provide `ambient_lux_x10`
- determine whether the scene is dark
- support context for optical conditions

### VL53L1X role

`VL53L1X` measures object distance in millimeters.

Purpose:

- provide the raw distance field
- support snow-state interpretation
- support snow-height output
- detect possible near obstruction conditions

### Final ToF interpretation rule

This is the exact rule we should say in the exam because it matches the current firmware logic:

- `>= 1500 mm` means zero snow / clear
- `1000 mm` to `1499 mm` means snow is present
- `< 1000 mm` means obstruction

### Final snow-height rule

The firmware uses `1500 mm` as the current zero-snow reference.

So:

`snow_height_mm = 1500 mm - object_distance_mm`

but only when the distance is in the snow zone and not in the obstruction zone.

That means:

- if distance is `1500 mm` or greater, snow height is effectively `0 mm`
- if distance drops below `1500 mm` but stays at or above `1000 mm`, snow height increases
- if distance goes below `1000 mm`, the firmware treats it as obstruction, not valid snow accumulation

### Laser path role

The laser subsystem is a beam-interruption path.

Purpose:

- detect beam state
- derive `presence_detected`
- derive `motion_detected`
- support fast digital event detection independently of the camera path

Meaning:

- `laser_signal_detected = 1` means the receiver sees the expected beam state
- `presence_detected = 1` means the beam logic indicates occupancy/interruption
- `motion_detected = 1` means recent beam transitions indicate motion

The firmware also supports runtime laser timing profiles:

- `default`
- `fast`
- `stable`

### WonderCam role

`WonderCam` is the classification sensor.

Purpose:

- classify precipitation state
- classify package / foreign-object state
- provide confidence-filtered scene interpretation

Current class mapping:

- class `1` -> none
- class `5` -> ice
- class `10` -> package
- class `12` -> snow

Filtering behavior:

- low-confidence classifications are rejected
- the same class must remain stable across multiple frames
- the accepted class is fused into the optical slice frame

## 5. Connection Path

### Local sensor path

`STM32 -> SPI1 -> SC18IS604 -> downstream I2C -> BH1750 / VL53L1X / WonderCam`

Why this matters:

- local sensors share the downstream sensor bus
- the STM32 native `I2C1` stays reserved for the upstream host/master board
- the bridge isolates local sensing traffic from the upstream control bus

### Laser path

`STM32 GPIO -> laser transmitter / laser receiver`

Why direct GPIO is correct:

- the signal is binary and timing-sensitive
- it does not need I2C abstraction
- it is simpler and faster to evaluate directly in the MCU

### Upstream system path

`Optical slice fused state -> packet formatter -> I2C1 slave interface -> master board`

In the current system:

- the STM32 acts as the optical slice endpoint on the upstream bus
- the master board can read status, configuration, and diagnostics packets
- `USART2` remains a secondary debug interface

## 6. Upstream Packet Structure

The clean exam answer is no longer hypothetical. The firmware now uses packetized upstream output on `I2C1`.

### Common packet header

Each packet starts with:

1. start byte `0xA5`
2. start byte `0x5A`
3. protocol version
4. packet type
5. payload length
6. sequence number

Each packet ends with:

- `crc16`

### Status packet

The status packet exports the fused optical slice frame:

1. `timestamp_ms`
2. `status_flags`
3. `ambient_lux_x10`
4. `object_distance_mm`
5. `snow_height_mm`
6. `precipitation_level_x10`
7. `precipitation_type`
8. `motion_detected`
9. `package_detected`
10. `presence_detected`
11. `dark_detected`
12. `camera_online`
13. `laser_online`
14. `laser_signal_detected`
15. `tof_range_status`

### Configuration packet

The configuration packet exports runtime operating state:

1. `timestamp_ms`
2. `snow_baseline_mm`
3. `laser_presence_assert_ms`
4. `laser_presence_release_ms`
5. `laser_motion_hold_ms`
6. `laser_profile`
7. config flags
8. sample/report timing
9. stale timing values

### Diagnostics packet

The diagnostics packet exports maintenance and soak-test visibility:

1. `timestamp_ms`
2. upstream TX count
3. upstream RX count
4. upstream error count
5. bridge recovery count
6. `BH1750` stale count
7. `VL53L1X` stale count
8. `WonderCam` stale count
9. `WonderCam` online count
10. health-event count
11. fault-event count
12. last health / fault timestamps
13. last health / fault codes
14. raw and filtered `WonderCam` classes
15. class streak
16. confidence

### Upstream command model

The current control model is single-byte command based.

Important commands:

- select status packet
- select configuration packet
- select diagnostics packet
- capture baseline
- clear baseline
- choose `default` / `fast` / `stable` laser profile
- reset diagnostics

For exam wording, we can say:

"The STM32 exposes multiple packet views over I2C1 and accepts simple host commands to select packet type, capture calibration state, and change runtime tuning."

## 7. Status Flags

The optical slice uses packed status bits to summarize health and scene state.

Important flags include:

- `BH1750 present`
- `BH1750 valid`
- `VL53L1X present`
- `camera present`
- `laser present`
- `SC18IS604 present`
- `motion detected`
- `package detected`
- `dark`
- `master link OK`
- `laser signal detected`
- `presence detected`
- `VL53 range valid`
- `snow height valid`
- `obstruction detected`

These flags are updated before the status packet is refreshed.

## 8. IRQ Utilization

### SysTick

`SysTick` is the main timing base.

It supports:

- sensor scheduling
- debounce timing
- stale-data timeouts
- motion hold windows
- packet timestamping
- periodic health reporting

### EXTI for `SC18_INT`

`SC18_INT` is connected to `PA9` through `EXTI9_5`.

Final role:

- notify the application that bridge-side activity happened
- let the main loop react without doing long SPI work inside the ISR

Best-practice explanation:

- the ISR sets a flag
- the main loop performs the actual bridge service and sensor polling

### I2C1 event interrupt

`I2C1_EV_IRQHandler()` is part of the real upstream interface.

Final role:

- detect that the master board addressed the optical slice
- transmit the selected packet
- receive single-byte host commands

### I2C1 error interrupt

`I2C1_ER_IRQHandler()` is also used.

Final role:

- detect bus faults
- recover the listen state
- keep the upstream slave interface healthy

### UART role

`USART2` is secondary.

It is used for:

- boot diagnostics
- health reporting
- live state visibility
- fault reporting during bench work

It is not the primary production data export path.

## 9. Final Software Flow

The system follows this cycle:

1. initialize GPIO, SPI, `I2C1`, `USART2`, and the bridge
2. verify the bridge is reachable
3. initialize `BH1750`, `VL53L1X`, and `WonderCam`
4. enable the laser transmitter and read the receiver state
5. poll ambient light from `BH1750`
6. poll ToF distance from `VL53L1X`
7. poll classification data from `WonderCam`
8. sample the laser beam state
9. derive:
   - dark state
   - presence state
   - motion state
   - precipitation type
   - package state
   - snow height
   - obstruction state
10. update status flags
11. refresh status, config, and diagnostics packets
12. expose the latest selected packet on upstream `I2C1`
13. optionally print debug text on `USART2`

## 10. Best Exam Wording

If we need one strong short answer, this is the version to memorize:

"Our optical slice is an STM32F303-based sensing node. The STM32 communicates with local sensors through an SC18IS604 SPI-to-I2C bridge and handles the laser path directly through GPIO. The BH1750 measures ambient light, the VL53L1X measures distance, and the WonderCam provides classification such as snow, ice, or package detection. The firmware fuses those inputs into one optical slice state, interprets distance as clear, snow, or obstruction using defined thresholds, derives snow height, and sends structured status data to the master board through I2C1. SysTick provides timing, SC18_INT provides bridge event notification, and the I2C interrupt path handles the upstream slave protocol."

## 11. What To Remember For The Exam

- local sensors are not on STM32 native `I2C1`; they are behind the `SC18IS604`
- `I2C1` is the upstream master-board interface
- the laser path is direct GPIO, not I2C
- `BH1750` gives ambient light
- `VL53L1X` gives raw distance
- `WonderCam` provides scene classification
- `>= 1500 mm` means clear / zero snow
- `1000..1499 mm` means snow
- `< 1000 mm` means obstruction
- snow height is derived relative to the `1500 mm` reference
- `presence_detected` and `motion_detected` come from the laser beam logic
- the STM32 exports packetized data over `I2C1`
- the upstream link supports status, configuration, and diagnostics views
- `SysTick` handles timing
- `SC18_INT` gives bridge event notification
- `I2C1` event and error interrupts handle upstream communication
- `USART2` is for debugging, not the main production interface
