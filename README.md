# Optical Slice Firmware

This repo is the STM32 firmware we are using for our optical slice node. The target is a `NUCLEO-F303K8` / `STM32F303K8T6`, and the firmware is built around the final sensor topology we want on the board:

- `STM32F303 <-> SPI1 <-> SC18IS604 <-> downstream sensor I2C bus`
- `STM32F303 <-> I2C1 <-> upstream master board`
- `STM32F303 GPIO <-> laser transmitter / laser receiver`

The important design choice here is that we are not using the STM32's native `I2C1` for the local optical sensors. `I2C1` is being held for the upstream master-board link. Local sensors are supposed to sit behind the `SC18IS604` SPI-to-I2C bridge, while the laser path is wired straight to STM32 GPIO.

## Hardware Layout

### MCU / board

- `STM32F303K8T6`
- `NUCLEO-F303K8`
- generated with `STM32CubeIDE`
- GNU Tools for STM32 toolchain (`14.3.rel1` in the generated `Debug/makefile`)

### Sensor-side addresses

- `BH1750` = `0x23`
- `BH1750` alternate = `0x5C`
- `VL53L1X` = `0x29`
- `WonderCam` = `0x32`

### Pin map

- `PB3` = `SPI1_SCK`
- `PB4` = `SPI1_MISO`
- `PB5` = `SPI1_MOSI`
- `PA4` = `SC18_CS`
- `PA8` = `SC18_RESET`
- `PA9` = `SC18_INT`
- `PB0` = `LASER_TX`
- `PA12` = `LASER_RX`
- `PB6` = `I2C1_SCL` to the master board
- `PB7` = `I2C1_SDA` to the master board
- `PA2` = `USART2_TX` (`VCP_TX`)
- `PA15` = `USART2_RX` (`VCP_RX`)

## What The Firmware Does Right Now

The app entry points are in `Core/Src/optical_slice_app.c`. `main()` just initializes the Cube peripherals and then calls:

- `OpticalSlice_Init()`
- `OpticalSlice_Run()`

At this point, the firmware can already do the following:

- bring up the `SC18IS604` bridge over `SPI1`
- print bridge boot diagnostics and version info over `USART2`
- read `BH1750` ambient light through the bridge
- mark the scene as dark when ambient light falls below `10.0 lx`
- initialize the `VL53L1X` through the bridge using the ST ULD driver
- start `VL53L1X` ranging, poll for data-ready, read distance, and clear interrupts
- drive the laser transmitter from `PB0`
- read the laser receiver on `PA12`
- poll the `WonderCam` over the bridge and decode its classification summary
- report a combined live status line over UART when important state changes happen

## Current Sensor Status

### Confirmed working path

- `SC18IS604` bridge transport
- downstream bridge-backed I2C transactions
- `BH1750` ambient light reads
- `VL53L1X` ID / init / ranging / distance reads
- laser TX/RX GPIO path
- UART debug output

### Partially integrated path

- `WonderCam` startup and polling are implemented in code
- the firmware reads the camera firmware register, switches to classification mode, enables the LED, and reads the class summary block
- the current classification mapping is:
  - class `1` -> none
  - class `5` -> ice
  - class `10` -> package
  - class `12` -> snow
- camera classifications are only accepted when confidence is at least `0.85` and the same class is stable for `4` frames

We would still treat the `WonderCam` path as integration in progress until it is fully bench-validated on the real hardware.

## UART Output

The debug port is `USART2` at `38400 8N1`.

On boot, we currently print:

- `OPTICAL boot`
- a one-line boot diagnostic snapshot
- `SC18IS604` version text if the bridge responds
- a `VL53PROBE` line that confirms the bridge can reach the `VL53L1X`

During runtime, the app prints a `LIVE` report when state changes matter. The report currently includes:

- ambient light in lux
- dark / not dark state
- `VL53L1X` distance and its coarse height bucket
- laser signal state
- package detection state
- precipitation type from `WonderCam`
- camera online / offline state

Two fields are present in the live frame but are still placeholders right now:

- `presence_detected`
- `motion_detected`

Those are formatted in the UART report, but the current sensor poll path still leaves them at `0`.

## VL53L1X Bridge Note

The `VL53L1X` bridge path depends on split register transactions.

What works on this hardware is:

1. write the 16-bit register address
2. perform a separate read transaction

The combined read-after-write style that some devices tolerate is not reliable here through the `SC18IS604`, so the `VL53L1X` access layer uses discrete write/read transactions instead.

The ST ULD package used in this repo is under:

- `STSW-IMG009/STSW-IMG009_v3.5.5`

## Build Notes

We can build this project in either of these ways:

### STM32CubeIDE

Open the project in `STM32CubeIDE` and build the `Debug` configuration.

### Generated makefile

From the repo root:

```bash
make -C Debug all
```

The generated artifacts land in `Debug/`, including:

- `Debug/Optical Slice.elf`
- `Debug/Optical Slice.map`
- `Debug/Optical Slice.list`

## Validation Helpers

We added a small validation module for bench checks:

- `Core/Inc/optical_slice_validation.h`
- `Core/Src/optical_slice_validation.c`

The main entry points are:

- `OpticalValidation_RunSnapshot()`
- `OpticalValidation_FormatReport()`

That code is useful for quick validation of:

- app poll health
- bridge presence
- `BH1750` readiness
- `VL53L1X` presence and valid range data
- laser GPIO readability
- laser beam detection

## What Is Still Not Finished

- the upstream master-board `I2C1` protocol is not implemented yet
- the STM32 is not acting as the final master-board-facing slave yet
- `snow_height_mm` is still a placeholder and is not derived from a mounted baseline yet
- `presence_detected` and `motion_detected` still need real logic behind them
- the `WonderCam` path needs more hardware validation before we would call it done
- the UART output is still bring-up/debug oriented, not a finalized external interface

## Repo Layout

- `Core/Src/optical_slice_app.c`: top-level app logic and UART reporting
- `Core/Src/optical_slice_sensors.c`: sensor bring-up and polling
- `Core/Src/vl53l1x_bridge.c`: `VL53L1X` bridge-backed ranging layer
- `Core/Src/sc18is604.c`: SPI-to-I2C bridge driver
- `Core/Src/optical_slice_validation.c`: bench validation helpers
- `docs/`: bench notes, weekly deliverables, and capture scripts

## Current Bottom Line

The project is past basic bring-up. The bridge path is up, ambient light and ToF ranging are working, the laser front end is integrated, and the firmware is already producing a combined live status report. The next real work is finishing the upstream interface, turning raw distance into snow-height data, and finishing / validating the `WonderCam` path.
