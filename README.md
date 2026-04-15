# Optical Slice Firmware

This repo is the STM32 firmware we are using for our optical slice node. The target is a `NUCLEO-F303K8` / `STM32F303K8T6`, and the firmware is organized around the board split we want to keep:

- `STM32F303 <-> SPI1 <-> SC18IS604 <-> downstream sensor I2C bus`
- `STM32F303 <-> I2C1 <-> upstream master board`
- `STM32F303 GPIO <-> laser transmitter / laser receiver`

The key architectural choice is still the same: we keep native `I2C1` for the upstream host/master-board interface, and we move the local optical sensors behind the `SC18IS604` SPI-to-I2C bridge. The laser front end stays on direct GPIO.

## Hardware Layout

### MCU / board

- `STM32F303K8T6`
- `NUCLEO-F303K8`
- generated with `STM32CubeIDE`
- GNU Tools for STM32 toolchain (`14.3.rel1` in the generated project files)

### Sensor-side addresses

- `BH1750` = `0x23`
- `BH1750` alternate = `0x5C`
- `VL53L1X` = `0x29`
- `WonderCam` = `0x32`

### Upstream address

- optical slice node on `I2C1` = `0x42` (7-bit slave address)

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

The app entry points are in `Core/Src/optical_slice_app.c`. `main()` initializes the Cube peripherals and then calls:

- `OpticalSlice_Init()`
- `OpticalSlice_Run()`

At the current state, the firmware does all of the following:

- brings up the `SC18IS604` bridge over `SPI1`
- probes and recovers the bridge if downstream traffic goes stale
- reads `BH1750` ambient light through the bridge
- marks `dark_detected` when ambient light falls below `10.0 lx`
- initializes the `VL53L1X` through the bridge using the ST ULD driver
- starts ranging, polls for data-ready, reads distance, and clears interrupts
- marks ToF validity only when the range status is valid
- interprets ToF distance using the current project rule:
  - `>= 1500 mm` = zero snow / clear
  - `1000..1499 mm` = snow zone
  - `< 1000 mm` = obstruction
- derives `snow_height_mm` relative to the `1500 mm` zero-snow reference
- drives the laser transmitter from `PB0`
- reads the laser receiver on `PA12`
- derives `presence_detected` and `motion_detected` from the laser beam state with hold/debounce timing
- polls the `WonderCam`, switches it into classification mode, and decodes class summaries
- filters camera classifications by confidence and stability before changing the reported precipitation/package state
- exposes a compact 3-line `USART2` live report that fits typical serial terminals
- emits periodic health reports over `USART2`
- exposes the latest frame to the upstream board over `I2C1` as a slave packet at address `0x42`

## Current Sensor Status

### Working path

- `SC18IS604` bridge transport
- downstream bridge-backed I2C transactions
- bridge recovery / re-initialization when the sensor side goes stale
- `BH1750` ambient light reads with stale-data timeout handling
- `VL53L1X` ID / init / ranging / distance reads
- ToF-valid gating, snow-height derivation, and obstruction detection
- laser TX/RX GPIO path
- presence/motion detection from the laser path
- `USART2` structured boot, health, live, and fault output
- upstream `I2C1` slave link with packetized status, config, and diagnostics transfer

### Integrated but still bench-sensitive

- `WonderCam` startup and polling are implemented in code
- the firmware reads the camera firmware register, switches to classification mode, enables the LED, and reads the class summary block
- the current classification mapping is:
  - class `1` -> none
  - class `5` -> ice
  - class `10` -> package
  - class `12` -> snow
- camera classifications are accepted only when confidence is at least `0.85` and the same class stays stable for `4` frames

We still treat the `WonderCam` path as the most hardware-sensitive part of the system. It is integrated and reported in the live frame, but it still needs more bench time before we would call it fully production-trusted.

## USART2 Output

The debug port is `USART2` at `38400 8N1`.

We currently use four message styles:

- `BOOT | ...` for startup diagnostics
- `HEALTH | ...` for periodic subsystem status
- `LIVE ...` for compact runtime state
- `FAULT | ...` for sensor or bus problems

The `HEALTH` line also carries a few compact bring-up fields:

- `BL` = snow baseline state
- `LP` = active laser profile
- `M` = upstream master-link state

The live report is intentionally compact and prints as three lines:

```text
LIVE <time> | Lux <value> | Dark <Y/N>
ToF <value> | Snow <value>
Laser <Y/N> | Pres <Y/N> | Mot <Y/N> | Pkg <Yes/No/Unk> | Prec <state> | Cam <On/Off>
```

That format is meant for bring-up and operator visibility on the serial console without horizontal wrapping.

## Upstream I2C1 Master-Link Packet

`I2C1` is now configured as the optical slice node's upstream slave interface. The current firmware listens at `0x42` and serves three packet views selected by single-byte commands:

- status packet
- configuration packet
- diagnostics packet

The status packet includes:

- packet header bytes `0xA5 0x5A`
- protocol version
- packet type
- payload length
- sequence number
- timestamp
- `status_flags`
- ambient light
- ToF distance
- snow height
- precipitation level and type
- motion / package / presence / dark state
- camera status
- laser status and laser-signal state
- ToF range status
- CRC16

The configuration packet includes runtime baseline and laser timing state. The diagnostics packet includes master-link counters, bridge recovery counters, stale counters, and recent health/fault tracking.

The packet assembly lives in `Core/Src/i2c.c`. The current control model uses single-byte commands for packet selection, baseline capture/clear, laser profile selection, and diagnostics reset.

## VL53L1X Bridge Note

The `VL53L1X` bridge path depends on split register transactions:

1. write the 16-bit register address
2. perform a separate read transaction

The combined read-after-write style is not reliable on this hardware through the `SC18IS604`, so the bridge-backed `VL53L1X` access layer uses discrete write/read transactions.

The ST ULD package used in this repo is under:

- `STSW-IMG009/STSW-IMG009_v3.5.5`

## Validation Helpers

We added a small validation module for bench checks:

- `Core/Inc/optical_slice_validation.h`
- `Core/Src/optical_slice_validation.c`

The main entry points are:

- `OpticalValidation_RunSnapshot()`
- `OpticalValidation_FormatReport()`

The validation snapshot is useful for quick checks of:

- app poll health
- bridge readiness
- `BH1750` readiness
- `VL53L1X` presence and valid-range data
- camera readiness
- snow-height validity
- baseline presence
- master-link readiness
- laser GPIO readability
- laser beam detection
- derived presence and motion state

## Build Notes

We can build the project in either of these ways:

### STM32CubeIDE

Open the project in `STM32CubeIDE` and build the `Debug` configuration.

## What Is Still Left To Make This Better

The project is well past basic bring-up, but a few areas still matter if we want the node to behave like a polished final system:

- bench-validate `WonderCam` behavior across the actual snow / ice / package cases we care about
- extend the upstream protocol beyond the current single-byte control model if we later need arbitrary configuration writes instead of command-based presets
- use the new upstream diagnostics packet and validation report during longer-duration soak testing for bridge recovery, camera freshness, and ToF freshness edge cases
- choose the final laser timing profile on the installed hardware after beam alignment and environmental testing
- confirm that the fixed `1500 mm` zero-snow reference and `1000 mm` obstruction threshold match the final installed mechanical geometry

## Repo Layout

- `Core/Src/optical_slice_app.c`: top-level app logic and `USART2` reporting
- `Core/Src/optical_slice_sensors.c`: sensor bring-up, polling, freshness, and derived-state logic
- `Core/Src/i2c.c`: upstream `I2C1` slave link and packet assembly
- `Core/Src/vl53l1x_bridge.c`: `VL53L1X` bridge-backed ranging layer
- `Core/Src/sc18is604.c`: SPI-to-I2C bridge driver
- `Core/Src/optical_slice_validation.c`: bench validation helpers
- `docs/`: project notes, exam notes, and supporting documentation

## Current Bottom Line

The firmware is no longer just a sensor bring-up skeleton. Our bridge path is up, ambient light and ToF ranging are working, the laser front end is feeding presence/motion logic, the UART output is structured for real debugging, and the upstream `I2C1` link now exposes status, configuration, and diagnostics. The biggest remaining work is physical calibration and hardware validation, not basic architecture.
