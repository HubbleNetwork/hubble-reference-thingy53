# Hubble Network Zephyr Reference - Thingy53

## Overview

This application implements a dual-mode communication system for the Nordic Thingy53 that alternates between Bluetooth Low Energy (BLE) beaconing and satellite transmission.

## Table of Contents

1. [Building the Application](#building-the-application)
2. [Provisioning a New Device](#provisioning-a-new-device)
3. [Flashing Thingy53 using nRF-DK as Programmer](#flashing-thingy53-using-nrf-dk-as-programmer)
4. [Transmission Logic](#transmission-logic)
5. [Hardware Button](#hardware-button)
6. [LED Indication](#led-indication)
7. [Shell Commands](#shell-commands)
8. [Configuration Options](#configuration-options)

---

## Building the Application

### Prerequisites

Before building, ensure you have a proper Zephyr development environment set up. Follow the official [Zephyr Getting Started Guide](https://docs.zephyrproject.org/latest/getting_started/index.html).

### Initial Setup

1. Install the Python dependencies used by the helper scripts in `scripts/`:

```bash
pip install -r scripts/requirements.txt
```

2. Initialize the workspace folder where the `hubble-thingy53` and all Zephyr modules will be cloned:

```bash
# Initialize workspace for the example-application (main branch)
west init -m https://github.com/HubbleNetwork/hubble-thingy53 --mr main hubble-workspace

# Update Zephyr modules
cd hubble-workspace
west update
west blobs fetch
```

### Building for Thingy53

```bash
cd hubble-thingy53
west build -p -b thingy53/nrf5340/cpuapp app --sysbuild
```

## Build Output

The build output will be in the `build/` directory. The application consists of two firmware images:
- **Application core (CPUAPP)**: Main application logic, BLE stack, and shell interface
- **Network core (CPUNET)**: BLE HCI stack or satellite transmission stack

---

## Provisioning a New Device

A device must have a unique 32-byte cryptographic key before it can transmit Hubble Network packets. There are two ways to provision the key.

### Option A: Compile-time (embedded in firmware)

```bash
# Build with the key baked in
west build -p -b thingy53/nrf5340/cpuapp app --sysbuild -- -DCONFIG_HUBBLE_DEVICE_KEY=\"<base64-encoded-key>\"

# Flash the device
west flash --runner nrfjprog -- --nrf-family=NRF53 --snr=<NRF_DK_SERIAL_NUMBER>
```

### Option B: Runtime via shell

Flash the firmware without a key, then set it over UART after boot:

```
HubbleNetwork:~$ key <base64-encoded-key>
```

The key is written to non-volatile storage (NVS) and survives reboots. On subsequent boots the device loads the key from NVS automatically, so this command only needs to be run once per device.

### Important Notes

- **Device Key Security**: Keep your device keys secure and never commit them to version control.
- **Key Format**: The key must be exactly 32 bytes. If using base64 encoding, ensure the decoded key is 32 bytes.

---

## Flashing Thingy53 using nRF-DK as Programmer

The Thingy53 can be flashed using an nRF Development Kit (nRF-DK) such as the nRF52840 DK or nRF5340 DK as a programmer.

### Hardware Setup

1. **Connect the nRF-DK to your computer** via USB
2. **Power the Thingy53** separately via USB (recommended)
3. **Connect the Thingy53 to the nRF-DK** using jumper wires:

| nRF-DK Pin | Thingy53 Pin | Description |
|------------|-------------|-------------|
| VTG        | VDD         | Power (3.3V) - Optional if Thingy53 is self-powered via USB |
| GND        | GND         | Ground (required) |
| SWDIO      | SWDIO       | Serial Wire Debug Data (required) |
| SWDCLK     | SWDCLK      | Serial Wire Debug Clock (required) |
| RESET      | RESET       | Reset signal (required) |

### Flashing Procedure

#### Method 1: Using West Flash (Recommended)

1. **Identify connected devices**:

```bash
nrfjprog --ids
```

2. **Flash the application** using west, specifying the nRF-DK serial number:

```bash
west flash --runner nrfjprog -- --nrf-family=NRF53 --snr=<NRF_DK_SERIAL_NUMBER>
```

**Example:**
```bash
west flash --runner nrfjprog -- --nrf-family=NRF53 --snr=683010000
```

3. **Verify the flash** (optional):

```bash
west flash --runner nrfjprog -- --nrf-family=NRF53 --snr=<NRF_DK_SERIAL_NUMBER> --verify
```

#### Method 2: Using nrfjprog Directly

```bash
# Identify the Thingy53 device ID
nrfjprog --ids

# Erase the Thingy53 device (optional, recommended for clean flash)
nrfjprog --eraseall --family NRF53 --snr=<THINGY53_SERIAL_NUMBER>

# Program the application core
nrfjprog --program build/app/zephyr/zephyr.hex --chiperase --family NRF53 --snr=<THINGY53_SERIAL_NUMBER>

# Program the network core
nrfjprog --program build/cpunet/zephyr/zephyr.hex --chiperase --family NRF53 --snr=<THINGY53_SERIAL_NUMBER>

# Reset and run
nrfjprog --reset --family NRF53 --snr=<THINGY53_SERIAL_NUMBER>
```

**Note:** When using nRF-DK as a programmer, `nrfjprog` will see both the nRF-DK and the Thingy53 as separate devices. Specify the Thingy53's serial number (not the nRF-DK's) when programming directly.

### Understanding Device Detection

When you connect both the nRF-DK and Thingy53, `nrfjprog --ids` will show two devices. To identify which is which:
- The nRF-DK typically has a known serial number pattern
- The Thingy53 will appear as a separate nRF53 device
- You can disconnect the Thingy53 temporarily to see which device disappears

### Troubleshooting

- **West flash fails**:
  - Verify you're specifying the nRF-DK serial number (not Thingy53) when using `--snr`
  - Check that both cores built successfully: `ls build/app/zephyr/zephyr.hex` and `ls build/cpunet/zephyr/zephyr.hex`

### Additional Resources

- [Building and Programming Thingy53](https://docs.nordicsemi.com/bundle/ncs-latest/page/nrf/app_dev/device_guides/thingy53/building_thingy53.html)
- [nRF Connect SDK Programming Guide](https://developer.nordicsemi.com/nRF_Connect_SDK/doc/latest/nrf/getting_started.html)

---

## Transmission Logic

The application implements a dual-mode transmission system that alternates between BLE beaconing and satellite transmission.

### System Architecture

The Thingy53 uses a dual-core architecture:
- **Application Core (CPUAPP)**: Runs the main application logic, BLE stack, and shell interface
- **Network Core (CPUNET)**: Can run either the BLE HCI stack or the satellite transmission stack

### Transmission Flow

#### 1. Initialization Phase

1. **Boot Sequence**:
   - Application core boots and sets a shared memory mark (`HUBBLE_SAT_MARK` at `0x2007F000`) to indicate BLE stack mode
   - Network core checks this mark on boot to determine which stack to initialize
   - Application core decodes the device key from `CONFIG_HUBBLE_DEVICE_KEY` (base64) and initializes Bluetooth

2. **Time Synchronization**:
   - Device advertises in connectable mode waiting for a companion app connection
   - Companion app provides UTC time via BLE GATT characteristic
   - Once UTC is received, the device transitions to beaconing mode

#### 2. BLE Beaconing Phase

1. **Advertisement Generation**:
   - Device generates BLE advertisement packets using Hubble Network protocol
   - Advertisement frequency is controlled by `CONFIG_HUBBLE_SAMPLE_ADV_FREQUENCY` (default: 28800 seconds = 8 hours)

2. **Satellite Window Scheduling**:
   - Device receives ephemeris data (satellite pass timestamps) from companion app
   - Up to 300 satellite pass windows can be stored
   - Each window contains a timestamp (Unix time in seconds) and duration (transmission window length in seconds)

3. **Timer Management**:
   - BLE advertisement timer runs continuously, refreshing advertisements periodically
   - Satellite transmission timer calculates time until next satellite pass
   - When satellite timer expires, device transitions to satellite transmission mode

#### 3. Satellite Transmission Phase

1. **Stack Switch**:
   - Application core stops BLE advertisements and disables Bluetooth
   - Sets `HUBBLE_SAT_MARK` to satellite stack mode (0)
   - Enables network core and waits for IPC endpoint binding (3-second timeout)
   - Network core detects the mark and initializes satellite transmission stack

2. **Packet Preparation**:
   - Application core prepares satellite IPC packet with:
     - Sensor data payload (temperature, pressure, humidity, or battery)
     - Retransmission frequency (0-10 seconds between packets)
     - Transmission duration (from ephemeris data)

3. **Transmission Loop**:
   - Network core enables satellite radio
   - For the duration of the transmission window:
     - Enables power amplifier (PA) GPIO
     - Transmits satellite packet
     - Waits for retransmission interval
     - Disables PA GPIO
     - Repeats until window duration expires
   - Network core sends completion acknowledgment via IPC

4. **Return to BLE**:
   - Application core receives IPC acknowledgment
   - Disables network core
   - Sets `HUBBLE_SAT_MARK` back to BLE mode (1)
   - Re-enables Bluetooth and resumes BLE beaconing

### Timing Considerations

- **BLE Advertisement**: Refreshed every `CONFIG_HUBBLE_SAMPLE_ADV_FREQUENCY` seconds (default: 8 hours)
- **Satellite Windows**: Scheduled based on ephemeris data received from companion app
- **Retransmission**: Configurable 0-10 seconds between packets during satellite transmission

---

## Hardware Button

The physical button (`sw0`) toggles Bluetooth on and off. Each press checks the current BLE state and switches it:

- **BLE on** → press button → BLE disabled
- **BLE off** → press button → BLE enabled

---

## LED Indication

When `CONFIG_APP_BLINK_LED` is enabled, an LED blinks for the duration of each satellite transmission window and turns off when the window ends.

The LED color is selected at build time via a Kconfig choice:

| Kconfig symbol          | LED alias | Color        |
|-------------------------|-----------|--------------|
| `APP_LED_RED` (default) | `led0`    | Red          |
| `APP_LED_GREEN`         | `led1`    | Green        |
| `APP_LED_BLUE`          | `led2`    | Blue         |

---

## Shell Commands

The application provides a shell interface for runtime configuration and monitoring. Access the shell via UART (115200 baud, 8N1). The shell prompt is `HubbleNetwork:~$ `.

### Command Reference

#### `key <base64-key>`

Sets the device cryptographic key at runtime. The key is persisted to NVS and loaded automatically on subsequent boots.

**Parameters:**
- `base64-key`: Base64-encoded 32-byte device key

**Example:**
```
key dGhpcyBpcyBhIHRlc3Qgb9V5IHRlc3Qgb5V5MQ==
```

---

#### `bluetooth <enable|disable>`

Enables or disables Bluetooth advertising. The state is persistent across satellite transmission cycles — if the user disables BLE, it will not be automatically re-enabled when the satellite window ends.

**Parameters:**
- `enable` - Enable BLE advertising
- `disable` - Disable BLE advertising

**Example:**
```
bluetooth disable
bluetooth enable
```

---

#### `retransmission <value>`

Sets the interval between packet transmissions during satellite transmission windows.

**Parameters:**
- `value`: Integer between 0 and 10 (seconds)

**Example:**
```
retransmission 2
```

**Default:** Configured via `CONFIG_HUBBLE_RETRANSMISSION_FREQUENCY` (default: 4 seconds)

---

#### `payload <type>`

Sets the sensor data type to include in satellite transmission payloads.

**Parameters:**
- `type`: One of the following:
  - `temperature` - Ambient temperature (BME680)
  - `pressure` - Atmospheric pressure (BME680)
  - `humidity` - Relative humidity (BME680)
  - `battery` - Battery voltage (fuel gauge)
  - `custom <string>` - Custom payload, up to 12 characters

**Example:**
```
payload temperature
payload battery
payload custom mydata
```

---

#### `sensor <type>`

Reads and displays the current value from the specified sensor.

**Parameters:**
- `type`: `temperature`, `pressure`, `humidity`, or `battery`

**Example:**
```
sensor temperature
```

**Output:**
```
Sensor temperature value 23.500000
```

---

#### `status`

Displays the current device status and transmission schedule.

**Output Examples:**

When waiting for time synchronization:
```
Waiting for time synchronization
```

When a cryptographic key has not been provisioned:
```
Waiting for hubble cryptographic key
```

When beaconing (BLE mode):
```
Beaconing
Next scheduled transmissions:
	01/01/2025 00:00:00 UTC (1735689600)
	01/01/2025 03:00:00 UTC (1735700400)
	01/01/2025 06:00:00 UTC (1735711200)
	01/01/2025 09:00:00 UTC (1735722000)
```

When transmitting to satellite:
```
Transmitting to satellite
```

**Information Displayed:**
- Current mode: "Beaconing" or "Transmitting to satellite"
- Next scheduled satellite transmission timestamps in `MM/DD/YYYY HH:MM:SS UTC (unix_ts)` format (up to 4 upcoming windows)
- Battery charge percentage and voltage (when fuel gauge is enabled)

---

### Shell Usage Tips

1. **Command History**: Use arrow keys to navigate command history
2. **Tab Completion**: Tab completion is available for commands
3. **Error Messages**: Invalid commands or parameters will display helpful error messages
4. **Real-time Configuration**: All configuration changes take effect for the next transmission window
5. **Persistent Settings**: Most runtime configuration changes (retransmission frequency, payload type) are not persisted across reboots. The exception is the device key set via the `key` command, which is saved to NVS.

---

## Configuration Options

### Build-time Configuration

#### `CONFIG_HUBBLE_DEVICE_KEY`

**Type:** String
**Default:** (empty)
**Description:** Base64-encoded 32-byte cryptographic device key.

---

#### `CONFIG_HUBBLE_RETRANSMISSION_FREQUENCY`

**Type:** Integer
**Default:** 4
**Description:** Default interval (in seconds) between packet retransmissions during satellite transmission windows.

```kconfig
config HUBBLE_RETRANSMISSION_FREQUENCY
    int "Interval between packets"
    default 4
```

---

#### `CONFIG_APP_BLINK_LED`

**Type:** Boolean
**Default:** disabled
**Description:** Enables LED blinking during satellite transmission windows. When enabled, a color sub-option must be chosen (see below).

---

#### `CONFIG_APP_LED_RED` / `CONFIG_APP_LED_GREEN` / `CONFIG_APP_LED_BLUE`

**Type:** Choice (mutually exclusive)
**Default:** `CONFIG_APP_LED_RED`
**Description:** Selects which on-board LED is used when `CONFIG_APP_BLINK_LED` is enabled. Maps to devicetree aliases `led0` (red), `led1` (green), and `led2` (blue) respectively.

---

#### `CONFIG_HUBBLE_SAMPLE_ADV_FREQUENCY`

**Type:** Integer
**Default:** 28800 (8 hours)
**Description:** Frequency (in seconds) at which new BLE advertisement packets are generated. Must be less than 24 hours (86400 seconds).

```kconfig
config HUBBLE_SAMPLE_ADV_FREQUENCY
    int "Frequency to generate new advertisement in seconds"
    default 28800  # 8 hours
```

---

### Runtime Configuration

The following settings can be changed at runtime via shell commands:

- **Retransmission Frequency**: `retransmission` command (0-10 seconds)
- **Payload Type**: `payload` command (temperature, pressure, humidity, battery, or custom string)

**Note:** Runtime configuration changes are not persisted across reboots.

---

## Additional Resources

- **Zephyr Project Documentation**: https://docs.zephyrproject.org/
- **Nordic Thingy53 Documentation**: https://developer.nordicsemi.com/nRF_Connect_SDK/doc/latest/nrf/boards/thingy53_nrf5340/doc/index.html
