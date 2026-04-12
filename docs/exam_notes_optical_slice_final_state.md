# Optical Slice Exam Notes: Final Finished System

This note is the clean "finished project" version of the optical slice that we can use for exam prep.

Important clarification:

- this document describes the intended final architecture of the optical slice
- the hardware direction is confirmed by the repo
- some implementation details below, especially the exact upstream packet layout, are an engineering inference based on the current frame structure and bus design because that final protocol is not fully implemented in code yet

If we have to explain the optical slice in an exam as if the system is fully complete, this is the version to use.

## 1. One-Paragraph Exam Answer

The optical slice is an STM32-based sensing node built on an `STM32F303K8T6` that collects ambient light, time-of-flight distance, laser beam state, and camera-based classification data. The STM32 communicates with the local optical sensors through an `SC18IS604` SPI-to-I2C bridge, while the laser transmitter and receiver are connected directly to STM32 GPIO. The optical slice fuses all sensor readings into a single application frame, converts raw distance into snow-height information, classifies precipitation and package presence, and then sends a finalized status packet to the master board over the STM32's native `I2C1` interface. Interrupts are used for timing, bridge event notification, and master-board communication, while the main loop performs sensor fusion and packet preparation.

## 2. Final Hardware Layout

### Controller

- MCU: `STM32F303K8T6`
- board platform: `NUCLEO-F303K8`

### Bus partition

The finished system has three communication layers:

1. local STM32 control layer
   The MCU controls reset lines, chip select, interrupt input, laser TX, laser RX, and debug UART.
2. sensor access layer
   The MCU uses `SPI1` to communicate with the `SC18IS604`.
3. downstream sensor layer
   The `SC18IS604` drives the local sensor-side I2C bus for optical sensors.

### Final signal ownership

- `SPI1` is used only for `SC18IS604` communication
- downstream sensor-side I2C is used by:
  - `BH1750`
  - `VL53L1X`
  - `WonderCam`
- direct GPIO is used by:
  - laser transmitter on `PB0`
  - laser receiver on `PA12`
- upstream `I2C1` is reserved for the master board link on:
  - `PB6` = `I2C1_SCL`
  - `PB7` = `I2C1_SDA`
- `USART2` is used for debug / bench visibility, not as the main production interface

### Final sensor addresses

- `BH1750` = `0x23`
- `BH1750` alternate = `0x5C`
- `VL53L1X` = `0x29`
- `WonderCam` = `0x32`

## 3. Libraries Used In The Finished System

### Platform libraries

- `STM32CubeIDE` generated project structure
- `STM32F3xx HAL`
- `CMSIS`

These are used for:

- GPIO control
- SPI transactions
- I2C peripheral control
- UART debug output
- EXTI dispatch
- SysTick-based timing

### Sensor libraries

- ST `VL53L1X` ULD package from `STSW-IMG009`

This library is used to:

- boot-check the `VL53L1X`
- initialize the sensor
- configure the ranging mode
- poll for new data
- read distance and range status
- clear sensor interrupts after each measurement

### Project firmware modules

- `optical_slice_app`
- `optical_slice_sensors`
- `sc18is604`
- `vl53l1x_bridge`
- `optical_slice_validation`

In the final system:

- `optical_slice_sensors` owns raw sensor acquisition
- `optical_slice_app` owns fusion, state decisions, and packet publication
- `sc18is604` owns bridge communication
- `vl53l1x_bridge` owns ToF bridge-safe access
- `optical_slice_validation` is used for bench verification and maintenance checks

## 4. Final Functional Behavior

In the finished optical slice, every input path is fully active.

### BH1750 role

`BH1750` measures ambient light level.

Final purpose:

- determine environmental brightness
- support a `dark_detected` flag
- provide context for optical conditions

### VL53L1X role

`VL53L1X` measures object distance in millimeters.

Final purpose:

- measure the distance from the sensor to the snow surface or target object
- provide the raw distance field
- support derived snow-height calculation

Final snow-height formula:

`snow_height_mm = calibrated_baseline_mm - object_distance_mm`

Where:

- `calibrated_baseline_mm` is the known distance from the installed optical slice to the ground or zero-snow reference plane
- `object_distance_mm` is the live `VL53L1X` measurement

This means:

- larger snow accumulation -> smaller measured object distance
- smaller snow accumulation -> larger measured object distance

### Laser path role

The laser subsystem is used as a binary beam-interruption path.

Final purpose:

- detect presence through beam break / beam restore logic
- support fast presence events
- support motion logic through assertion / release timing windows

Final meaning:

- `laser_signal_detected = 1` means the receiver sees the expected beam state
- `presence_detected = 1` means the monitored path is occupied or interrupted according to the chosen beam logic
- `motion_detected = 1` means recent beam transitions indicate movement through the slice

### WonderCam role

`WonderCam` is the classification sensor.

Final purpose:

- detect precipitation category
- detect package / foreign object category
- provide confidence-filtered scene interpretation

Final classification mapping:

- class `1` -> none
- class `5` -> ice
- class `10` -> package
- class `12` -> snow

Final filtering behavior:

- a classification is only accepted above the confidence threshold
- the same result must remain stable across multiple frames
- the accepted class is then fused into the optical slice state packet

## 5. Final Connection Path

### Local sensor path

The final local data path is:

`STM32 -> SPI1 -> SC18IS604 -> downstream I2C -> BH1750 / VL53L1X / WonderCam`

This path is used because:

- the local sensors share a common sensor-side I2C network
- the STM32 native `I2C1` is preserved for the master board
- the bridge makes the optical slice modular and electrically separated from the upstream control bus

### Laser path

The laser path is separate from the bridge:

`STM32 GPIO -> laser transmitter / laser receiver`

This is intentionally direct because the laser path is a fast binary digital signal and does not need I2C abstraction.

### Upstream system path

The final system output path is:

`Optical slice fused state -> STM32 packet formatter -> I2C1 -> master board`

In the finished system:

- the STM32 acts as the optical slice endpoint on the master board bus
- the master board reads a structured optical slice status packet
- UART remains available only for debugging and lab bring-up

## 6. Final Packet Structure

The repo already defines the optical slice software frame in `optical_slice_frame_t`. In a finished system, the cleanest exact upstream design is to export that frame to the master board in a compact packet with header and integrity checking.

Because the repo does not yet contain a finalized byte-level master-board spec, the packet below is the final-state format we should describe in the exam. It is directly aligned with the current frame fields.

### Proposed final upstream packet

1. `start_byte`
   Value: `0xAA`
   Purpose: frame start marker
2. `node_id`
   Value: optical slice node ID
   Purpose: identify this sensor node on the system bus
3. `packet_type`
   Value: optical slice status packet
   Purpose: distinguish status from diagnostics or commands
4. `payload_length`
   Purpose: payload size in bytes
5. `sequence_counter`
   Purpose: incrementing packet counter
6. `timestamp_ms`
   Purpose: age / timing of the frame
7. `status_flags`
   Purpose: packed health and detection flags
8. `ambient_lux_x10`
   Purpose: ambient light in tenths of lux
9. `object_distance_mm`
   Purpose: raw `VL53L1X` distance
10. `snow_height_mm`
    Purpose: derived snow-height output
11. `precipitation_level_x10`
    Purpose: precipitation confidence or score in tenths
12. `precipitation_type`
    Purpose: none / snow / ice / unknown
13. `motion_detected`
    Purpose: motion state
14. `package_detected`
    Purpose: package detection state
15. `presence_detected`
    Purpose: occupancy / beam-break state
16. `dark_detected`
    Purpose: low-light state
17. `camera_online`
    Purpose: WonderCam health state
18. `laser_online`
    Purpose: laser subsystem health state
19. `laser_signal_detected`
    Purpose: raw beam state
20. `tof_range_status`
    Purpose: raw range-quality code from `VL53L1X`
21. `crc16`
    Purpose: packet integrity check

### Why this packet is the right final answer

This is the most defensible exam answer because:

- it matches the existing firmware frame fields exactly
- it separates transport metadata from sensor payload
- it includes integrity protection
- it is simple enough for an embedded master board to parse reliably

## 7. Final Status Flags

The optical slice uses packed status bits to summarize sensor health and scene state.

Final status flags include:

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

In the final system, these flags are always updated before a new packet is exported to the master board.

## 8. Final IRQ Utilization

This section is important because it explains how the finished optical slice behaves as a real embedded system instead of just a polling demo.

### SysTick

`SysTick` is the global timing base.

Final use:

- sensor scheduling
- debounce timing
- timeout supervision
- motion hold windows
- packet age stamping
- periodic housekeeping

### EXTI for `SC18_INT`

`SC18_INT` is connected to `PA9` and routed through `EXTI9_5`.

Final use:

- detect when the bridge or bridge-connected device has an event pending
- wake or notify the main application that a sensor-side service action is needed
- reduce unnecessary blind polling

Best-practice ISR behavior:

- do not perform long SPI transactions inside the ISR
- only set a flag
- let the main loop or scheduler perform the actual bridge service

That means the final role of `SC18_INT` is event notification, not full transaction execution inside interrupt context.

### I2C1 event interrupt

The finished system uses `I2C1_EV_IRQHandler()` for the master board interface.

Final use:

- respond when the master board addresses the optical slice
- transmit the latest fused optical packet
- receive commands such as status request, reset request, or calibration request

In the finished design, this interrupt is important because the optical slice is no longer just logging over UART. It becomes a real bus participant in the larger system.

### I2C1 error interrupt

In a polished final system, the `I2C1` slave interface should also use an error-handling interrupt path.

Final use:

- detect bus errors
- detect arbitration or framing issues as applicable
- recover the interface cleanly
- preserve link reliability to the master board

This is not central in the current repo, but it is the correct final-system explanation for exam purposes.

### UART interrupt role

In the final system, UART is secondary.

Final use:

- debug logging
- field diagnostics
- service / maintenance output

UART is not the main exported data path in the final design.

## 9. Final Software Flow

The finished optical slice follows this cycle:

1. initialize GPIO, SPI, I2C1, UART, and the bridge
2. verify the bridge is reachable
3. initialize `BH1750`, `VL53L1X`, and `WonderCam`
4. enable the laser transmitter and confirm laser receiver behavior
5. start periodic sensing
6. read ambient light from `BH1750`
7. read distance from `VL53L1X`
8. read classification from `WonderCam`
9. read laser beam state from GPIO
10. derive:
    - dark state
    - presence state
    - motion state
    - precipitation type
    - package state
    - snow height
11. update status flags
12. pack the finalized optical slice frame
13. expose the latest packet to the master board through `I2C1`
14. optionally publish debug text on `USART2`

## 10. Best Exam Wording For "How Everything Works"

If asked to explain the project portion clearly, this is the strongest short answer:

"Our optical slice is an STM32F303-based sensing node. The STM32 talks to local optical sensors through an SC18IS604 SPI-to-I2C bridge, while the laser transmitter and receiver are handled directly through GPIO. The BH1750 provides ambient light, the VL53L1X provides distance for snow-height calculation, and the WonderCam provides classification such as snow, ice, or package detection. The MCU fuses all of these signals into one structured optical slice status packet and sends that packet to the master board over I2C1. SysTick provides timing, SC18_INT provides bridge event notification, and the I2C interrupt path handles communication with the master board."

## 11. What To Remember For The Exam

- local sensors are not on the STM32 native `I2C1`; they are behind the `SC18IS604`
- `I2C1` is the upstream master-board interface
- the laser path is direct GPIO, not I2C
- `BH1750` gives ambient light
- `VL53L1X` gives raw distance
- snow height is derived from calibrated baseline minus measured distance
- `WonderCam` provides scene classification
- the STM32 fuses all sensor outputs into one optical slice packet
- `SysTick` handles timing
- `EXTI` on `SC18_INT` handles bridge event notification
- `I2C1` interrupts handle master-board communication
- UART is for debug, not the final production data interface
