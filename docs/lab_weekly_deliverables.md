# Optical Slice Weekly Deliverables

## 1. Skeleton code created for the slice

The optical slice firmware skeleton is now in place. The STM32 talks to the `SC18IS604` over `SPI1`, uses the bridge for the `BH1750`, `VL53L1X`, and later the `WonderCam`, and keeps `I2C1` reserved for the master board. The laser path is now direct on STM32 GPIO, with `PB0` driving the transmitter and `PA12` reading the receiver.

## 2. Test functions created for validation

I added a validation module in:

- `Core/Inc/optical_slice_validation.h`
- `Core/Src/optical_slice_validation.c`

The main validation entry points are:

- `OpticalValidation_RunSnapshot()`
- `OpticalValidation_FormatReport()`

These functions are intended to support bench validation by checking whether:

- the sensor poll loop runs
- the `SC18IS604` bridge is reachable
- the `BH1750` is returning valid data
- the `VL53L1X` is present
- the `VL53L1X` is returning a valid range
- the laser receiver GPIO is readable
- the laser beam is currently detected

## 3. Validation plan for the circuit board

Recommended bring-up order:

1. Visual inspection before power:
   - check orientation of the STM32, `SC18IS604`, `BH1750`, `VL53L1X`, connectors, and pull-ups
   - inspect for solder bridges, missing joints, and reversed parts
2. Power-only bring-up:
   - power the board without sensors first
   - measure `3.3V`, ground continuity, and reset levels
   - confirm there is no abnormal current draw
3. MCU bring-up:
   - confirm SWD programming works
   - confirm UART boot text is visible
4. Bridge bring-up:
   - verify `SPI1` activity on `PB3/PB4/PB5`
   - verify `SC18_CS` on `PA4`
   - verify `SC18_RESET` on `PA8`
- verify `SC18_INT` idle level on `PA9`
5. Laser bring-up:
   - confirm `PB0` can enable the transmitter
   - confirm `PA12` changes state when the receiver sees and loses the beam
6. Sensor bring-up:
   - verify `BH1750` value changes with light level
   - verify `VL53L1X` returns a distance that changes with target position
7. System bring-up:
   - run the optical frame output continuously
   - verify sensor flags and values match physical conditions

Measurements to make during validation:

- `3.3V` rail with DMM
- board current draw during idle and active sensor reads
- `SPI1` waveforms with scope or logic analyzer
- `I2C1` master-board bus idle level
- laser receiver voltage at `PA12`
- `VL53L1X` measured distance versus actual ruler distance

## 4. Bench tests to communicate with sensors and actuator path

Bench tests to run:

- boot test: verify `OPTICAL boot` appears on UART
- bridge test: confirm `SC18IS604` version string appears
- light test: cover/uncover `BH1750` and observe `lux_x10`
- laser test: align and break beam, then observe `laser_sig`
- ToF test: move a flat target and observe `tof_mm`
- repeatability test: hold a fixed target and record multiple `tof_mm` readings
- noise test: observe readings with no target motion for drift

## 5. Parts collection checklist

Parts to collect or confirm:

- STM32 optical slice board
- `SC18IS604PWJ`
- `BH1750`
- `VL53L1X`
- `WonderCam`
- `KY-008` laser transmitter
- laser receiver module
- pull-up resistors and passives required by the PCB
- headers, test wires, and bench supply
- ST-Link / SWD programmer
- USB cable for UART debug
- DMM and, if available, oscilloscope or logic analyzer

## 6. GUI mockup direction

The GUI mockup should show the optical slice as a status dashboard with:

- ambient light level
- raw ToF distance
- derived snow height placeholder
- laser beam detected / not detected
- camera online / offline
- bridge online / offline
- timestamp of last valid update

For now, the GUI can treat snow height as "raw distance pending calibration" until the baseline conversion is finalized.

## 7. One-paragraph current state summary

Our optical slice firmware skeleton is in place and matches the final hardware architecture. The STM32 communicates with the `SC18IS604` over `SPI1`, uses the bridge to access the `BH1750` and `VL53L1X`, and keeps `I2C1` reserved for the master board. The laser path has been finalized as direct STM32 GPIO, with `PB0` driving the transmitter and `PA12` reading the receiver. The current code can boot, initialize the bridge, sample ambient light, read the laser receiver state, start and poll `VL53L1X` ranging through the bridge, and send a status frame over UART for bring-up. The main remaining work is to integrate the `WonderCam`, define the snow-height conversion and calibration flow, and finalize the communication interface to the master board.
