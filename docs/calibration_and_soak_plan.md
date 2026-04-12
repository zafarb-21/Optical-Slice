# Calibration And Soak Plan

This note captures the runtime controls and test flow we can use now that the upstream `I2C1` link exposes configuration and diagnostics packets.

## Upstream Address

- optical slice node address: `0x42` (7-bit)

## Single-Byte Commands

These commands are sent as a one-byte write to the optical slice node before any follow-up read.

- `0x00`: select status packet
- `0x01`: select configuration packet
- `0x02`: select diagnostics packet
- `0x10`: capture snow baseline from the current valid `VL53L1X` measurement
- `0x11`: clear the stored snow baseline
- `0x20`: select default laser timing profile
- `0x21`: select fast laser timing profile
- `0x22`: select stable laser timing profile
- `0x30`: reset diagnostics counters

## Packet Usage

### Status packet

Use this for normal master-board polling. It carries the fused runtime frame:

- timestamp
- status flags
- ambient light
- ToF distance
- snow height
- precipitation state
- motion / package / presence state
- dark state
- camera state
- laser state
- ToF range status

### Configuration packet

Use this to confirm runtime calibration and timing state:

- current snow baseline
- laser presence-assert timing
- laser presence-release timing
- laser motion-hold timing
- active laser profile
- baseline-valid flag
- master-link health/activity flags
- sample/report/stale timing values

### Diagnostics packet

Use this during bench work and soak testing:

- upstream TX / RX / error counters
- bridge recovery count
- `BH1750` stale count
- `VL53L1X` stale count
- `WonderCam` stale count
- `WonderCam` online count
- health-event count
- fault-event count
- last health / fault timestamps
- last health / fault codes
- raw and filtered `WonderCam` classes
- candidate streak
- filtered confidence

## Snow Baseline Capture Flow

The firmware can now store the snow baseline at runtime without recompiling.

Recommended flow:

1. mount the optical slice in its final geometry
2. point the `VL53L1X` at the true zero-snow reference plane
3. confirm the status packet shows a valid ToF range
4. send command `0x10`
5. read the configuration packet and confirm the baseline field is no longer `0xFFFF`
6. confirm live UART output no longer says `Snow uncalibrated`

If the geometry changes, clear and recapture:

1. send `0x11`
2. realign the node
3. send `0x10` again at the correct reference plane

## Laser Profile Tuning Flow

We now support three profile presets:

- `default`: current balanced timing
- `fast`: more responsive to short beam interruptions
- `stable`: more conservative against noise and jitter

Recommended flow:

1. start with `default`
2. test expected motion speed and beam alignment
3. switch to `fast` if we are missing short occupancy events
4. switch to `stable` if we are seeing false presence or motion from noise
5. log diagnostics and UART output for each profile before choosing the final setting

## WonderCam Bench Validation

This still requires real hardware and real scene examples. The code path is implemented, but we should validate it against the conditions we care about:

- no precipitation / no package
- package present
- snow present
- ice present
- low-light scene
- repeated scene transitions

For each case, log:

- raw class
- filtered class
- confidence
- candidate streak
- online / stale behavior

The diagnostics packet now exposes those fields so we can verify whether misclassification is coming from confidence filtering, class instability, or actual sensor dropout.

## Soak Test Targets

Use the diagnostics packet and validation snapshot while running long-duration tests.

Recommended observations:

- bridge recovery count stays at `0` in steady-state
- stale counters stay at `0` or remain explainable during forced fault tests
- fault-event count does not drift upward during stable idle operation
- `WonderCam` online count does not oscillate unexpectedly
- master-link error count remains at `0`
- baseline stays valid for the entire run

## Current Practical Limitation

We now have the software support to calibrate the node and tune the laser timing at runtime, but we still need the actual installed geometry and bench conditions to decide the final baseline and final laser profile. The firmware can support that work; it cannot invent the real hardware measurement on its own.
