# MQTT Publisher — Comprehensive Reference Guide

**A multi-threaded MQTT publisher for Linux embedded systems**

Multi-threaded MQTT metric publisher that reads metrics from an INI configuration file and publishes them to an MQTT broker at specified intervals. Designed for automation, home automation, and monitoring on resource-constrained devices such as Raspberry Pi.

**Version:** v1.0  
**License:** GPLv3  
**Author:** (c) 2026, Chapvic  
**Repository:** [mqtt_pub on GitHub](https://github.com/chapvic/mqtt_pub)

---

## Table of Contents

- [Overview](#overview)
- [Features](#features)
- [Requirements and Build](#requirements-and-build)
- [Installation](#installation)
- [Usage](#usage)
- [Configuration File](#configuration-file)
  - [File Format](#file-format)
  - [Section [MQTT]](#section-mqtt)
  - [Metric Sections](#metric-sections)
  - [Parameter: source](#parameter-source)
  - [Parameter: key](#parameter-key)
  - [Parameter: interval](#parameter-interval)
  - [Parameter: qos](#parameter-qos)
  - [Parameter: topic](#parameter-topic)
- [Source Types](#source-types)
  - [File Sources](#file-sources)
  - [Shell Command Sources](#shell-command-sources)
  - [Remote Broker Sources](#remote-broker-sources)
- [Key Extraction](#key-extraction)
  - [Literal Key](#literal-key)
  - [Regex Key](#regex-key)
  - [No Extraction (key=-)](#no-extraction-key--)
  - [Regex Reference](#regex-reference)
- [procfs Compatibility](#procfs-compatibility)
  - [How procfs Differs](#how-procfs-differs)
  - [DHT Driver Integration](#dht-driver-integration)
- [Raspberry Pi Metrics Catalog](#raspberry-pi-metrics-catalog)
  - [Temperature](#temperature)
  - [Memory](#memory)
  - [CPU Load](#cpu-load)
  - [Disk and Filesystem](#disk-and-filesystem)
  - [Network](#network)
  - [Clock Speeds and Voltages](#clock-speeds-and-voltages)
  - [Throttling Status](#throttling-status)
  - [DHT Sensor Data](#dht-sensor-data)
- [Shell Command Optimization](#shell-command-optimization)
- [Remote Broker Bridging](#remote-broker-bridging)
  - [Architecture](#architecture)
  - [Event-Driven vs Timer Mode](#event-driven-vs-timer-mode)
  - [Shared Connections](#shared-connections)
  - [Auto-Reconnect](#auto-reconnect)
  - [Fault Tolerance](#fault-tolerance)
- [How It Works](#how-it-works)
  - [Lifecycle](#lifecycle)
  - [Thread Model](#thread-model)
  - [Thread Safety](#thread-safety)
  - [Connection Callbacks](#connection-callbacks)
- [Signal Handling](#signal-handling)
- [QoS Levels](#qos-levels)
- [Debug Mode](#debug-mode)
- [Buffer Limits](#buffer-limits)
- [Validation Rules](#validation-rules)
- [Troubleshooting](#troubleshooting)
- [Best Practices](#best-practices)
- [Integration Examples](#integration-examples)
- [License](#license)

---

## Overview

MQTT Publisher is a lightweight C program designed to collect system metrics from various sources and publish them to an MQTT broker. It is purpose-built for automation scenarios — from home automation on a Raspberry Pi to industrial monitoring on embedded Linux boards.

The program reads a single INI configuration file that describes the MQTT broker connection and a list of metrics. Each metric specifies where to read data from (file, shell command, or remote MQTT broker), how often to publish it, and which MQTT topic to use.

### Design Philosophy

- **Minimal dependencies** — only libmosquitto and pthreads, both available on virtually every Linux distribution.
- **Low overhead** — each metric runs in its own thread with configurable intervals; no polling daemon, no scripting runtime.
- **procfs-friendly** — file sources use `open()`+`read()` instead of `fopen()`+`fread()` to avoid stdio buffering issues on pseudo-filesystems.
- **Fault-tolerant** — if the broker is unreachable at startup, the program retries silently. If a remote broker goes down, local metrics continue publishing.
- **Configuration-driven** — no hardcoded metrics, no compiled-in thresholds. Everything is in one INI file.

### Typical Use Cases

| Scenario | How |
|----------|-----|
| Home automation | Publish temperature, humidity, and CPU load to Home Assistant via MQTT |
| Raspberry Pi monitoring | Stream CPU/GPU temperature, memory, disk to a central broker |
| Sensor data collection | Read DHT11/DHT22 sensors via the [DHT driver](https://github.com/chapvic/dht-driver) procfs interface and republish over MQTT |
| Multi-device aggregation | Bridge metrics from remote MQTT brokers to a central one |
| Industrial monitoring | Read PLC data via shell scripts and publish to SCADA MQTT broker |

---

## Features

- **Multi-threaded publishing** — each metric runs in its own thread with an independent interval.
- **Three source types** — file paths, shell commands (`@`-prefix), and remote MQTT brokers (`$`-prefix).
- **Key extraction** — extract a specific value from multi-line output using literal key matching (`key=T`) or POSIX Extended Regular Expressions (`key=/pattern/`).
- **Remote broker bridging** — subscribe to a remote broker topic and republish received values to the local broker.
- **Hybrid mode** — remote metrics support event-driven publishing (no `interval`) or timer-based republishing (with `interval`).
- **Shared connections** — remote broker connections are deduplicated by `host:port`, so multiple metrics from the same broker share one connection.
- **Auto-reconnect** — automatic reconnection to both local and remote brokers with bounded delay (1–60 seconds).
- **Non-blocking remote setup** — if a remote broker is unreachable, local metrics are unaffected.
- **INI configuration** — all settings in one file; no command-line flags for metrics.
- **Authentication** — username/password support for local and remote brokers.
- **QoS per metric** — each metric can use QoS 0, 1, or 2.
- **Graceful shutdown** — clean teardown on SIGINT/SIGTERM with thread join and resource cleanup.
- **Debug mode** — verbose logging of every publish, connection event, and error.
- **Config validation** — comprehensive validation with descriptive error messages at startup.
- **systemd integration** — Makefile with `install`/`uninstall` targets, systemd service unit, and environment file.

---

## Requirements and Build

### Dependencies

- **GCC** (or compatible C compiler)
- **libmosquitto** — Mosquitto client library (`libmosquitto-dev` on Debian/Ubuntu, `mosquitto-devel` on Fedora/RHEL)
- **pthread** — POSIX threads (part of glibc on all Linux systems)

### Manual Build

```bash
gcc -O2 -Wall -Wextra -Werror -o mqtt_pub mqtt_pub.c -pthread -lmosquitto
```

| Flag | Purpose |
|------|---------|
| `-O2` | Second-level optimization |
| `-Wall -Wextra` | All major compiler warnings |
| `-Werror` | Treat warnings as errors |
| `-pthread` | Link POSIX threads library |
| `-lmosquitto` | Link Mosquitto client library |

The result is the `mqtt_pub` executable in the current directory.

---

## Installation

### Makefile Targets

The project includes a Makefile with the following targets:

| Target | Description |
|--------|-------------|
| `make` or `make all` | Compile `mqtt_pub` |
| `make install` | Install binary, systemd unit, config, and env file; start service |
| `make uninstall` | Stop service, remove all installed files (preserves `install.log`) |
| `make clean` | Remove compiled binary |
| `make distclean` | Remove binary and `install.log` |

### Installation Parameters

Parameters can be passed on the `make install` command line:

| Parameter | Default | Description |
|-----------|---------|-------------|
| `HOST` | `127.0.0.1` | MQTT broker hostname or IP |
| `PORT` | `1883` | MQTT broker port |
| `MQTT_USER` | _(empty)_ | Username for broker authentication |
| `MQTT_PASS` | _(empty)_ | Password for broker authentication |
| `CONF` | `/etc` | Directory for configuration file |

### Installation Examples

```bash
# Default install (localhost:1883, no auth, config in /etc)
make install

# Custom broker and auth
make install HOST=192.168.1.100 PORT=1883 MQTT_USER=monitor MQTT_PASS=secret

# Custom config directory
make install CONF=/etc/mqtt_pub

# Full custom
make install HOST=broker.local PORT=8883 MQTT_USER=admin MQTT_PASS=pass123 CONF=/etc/mqtt_pub
```

### Config File Handling During Install

The `make install` target follows this priority:

1. **If `mqtt_pub.ini` exists in the project directory** — it is copied to the destination, and any command-line overrides (`HOST`, `PORT`, `MQTT_USER`, `MQTT_PASS`) are applied on top via `sed`. This preserves user-preconfigured metrics.
2. **If no local config and no config at destination** — a minimal config is generated dynamically with the provided (or default) parameters.
3. **If no local config but a config exists at destination** — only the override parameters are updated; existing metrics are preserved.

### What Gets Installed

| File | Destination | Description |
|------|-------------|-------------|
| `mqtt_pub` | `/usr/local/bin/mqtt_pub` | Executable binary |
| `mqtt_pub.service` | `/etc/systemd/system/mqtt_pub.service` | systemd service unit |
| `mqtt_pub.ini` | `$CONF/mqtt_pub.ini` | Configuration file |
| env file | `/etc/default/mqtt_pub` | Environment file with `PARAMS=-f <config_path>` |
| `install.log` | project dir | Records the `CONF` path for uninstall |

### systemd Service Management

```bash
# Start service
systemctl start mqtt_pub

# Stop service
systemctl stop mqtt_pub

# Restart service
systemctl restart mqtt_pub

# Check status
systemctl status mqtt_pub

# Enable autostart
systemctl enable mqtt_pub

# Disable autostart
systemctl disable mqtt_pub

# View logs
journalctl -u mqtt_pub -f
```

### Uninstall

```bash
make uninstall
```

This stops and disables the service, removes the binary, systemd unit, env file, and config file. The `install.log` is preserved in the project directory for reference.

---

## Usage

### Command-Line Syntax

```
mqtt_pub [-f <config_file>] [-h]
```

| Option | Description |
|--------|-------------|
| `-f <config_file>` | Full path to the INI configuration file. Default: `mqtt_pub.ini` in the current directory. |
| `-h` | Print help and exit. |

### Examples

```bash
# Start with default config (mqtt_pub.ini in current directory)
./mqtt_pub

# Start with a specific config file
./mqtt_pub -f /etc/mqtt_pub/my_metrics.ini

# Print help
./mqtt_pub -h
```

### Running as a systemd Service

After `make install`, the service is managed by systemd. The environment file `/etc/default/mqtt_pub` contains:

```
PARAMS=-f /etc/mqtt_pub.ini
```

To change the config file path, edit the env file and restart:

```bash
echo 'PARAMS=-f /etc/mqtt_pub/production.ini' | sudo tee /etc/default/mqtt_pub
sudo systemctl restart mqtt_pub
```

---

## Configuration File

The configuration is in INI format. The file consists of an optional `[MQTT]` section (broker connection settings) and one or more metric sections.

### File Format

- Lines starting with `#` or `;` are comments and are ignored.
- Empty lines are ignored.
- Each section begins with a `[section_name]` header.
- Parameters are specified as `key=value`.
- Leading and trailing whitespace on both key and value is trimmed.

### Section [MQTT]

The broker connection settings section. If absent, defaults are used: host `localhost`, port `1883`, no authentication.

**The `[MQTT]` section must be the first section in the configuration file if present.**

| Parameter | Description | Default | Limit |
|-----------|-------------|---------|-------|
| `host` | MQTT broker address (hostname or IP) | `localhost` | 256 bytes |
| `port` | MQTT broker port | `1883` | 1–65535 |
| `user` | Username for authentication | — | 64 bytes |
| `password` | Password for authentication | — | 256 bytes |
| `debug` | Debug mode: `0` = quiet, `1` = verbose | `0` | — |

**Authentication:** The authentication flag is enabled when either `user` or `password` (or both) is specified. If only `password` is given without `user`, an empty string is sent as the username — the broker decides whether to accept such a connection.

### Metric Sections

Each metric is described by a separate section with a unique name. The section name is used in diagnostic messages and startup output.

| Parameter | Description | Required | Default | Limit |
|-----------|-------------|----------|---------|-------|
| `topic` | MQTT topic to publish the metric value to | Yes | — | 512 bytes |
| `interval` | Publish interval in seconds | Yes (no for `$`-source without interval) | — | > 0 |
| `source` | Data source: file path, `@shell_command`, or `$remote` | Yes | — | 2048 bytes |
| `qos` | QoS level for publishing: `0`, `1`, or `2` | No | `0` | 0–2 |
| `key` | Key for value extraction (literal, regex, or `-`) | No | _(empty)_ | 64 bytes |

**Uniqueness:** Metric section names and MQTT topics must be unique within the configuration file. Duplicates cause a startup error.

### Parameter: source

The `source` parameter determines where the program reads the metric value from. Three modes are supported, distinguished by the first character:

| Prefix | Mode | Example |
|--------|------|---------|
| _(none)_ | File path — opened with `open()`, read with a single `read()` | `source=/sys/class/thermal/thermal_zone0/temp` |
| `@` | Shell command — executed via `popen()`, stdout is captured | `source=@vcgencmd measure_temp \| awk -F= '{print $2}'` |
| `$` | Remote MQTT broker — subscribe to a topic and republish | `source=$192.168.1.50:1883/sensors/temp` |

See [Source Types](#source-types) for detailed documentation.

### Parameter: key

The `key` parameter controls how the raw source content is processed before publishing. Three modes:

| Value | Mode | Description |
|-------|------|-------------|
| _(empty or absent)_ | Full payload | The entire source content is trimmed and published as-is |
| `key_name` | Literal key | Search line-by-line for `key_name=<value>`, publish `<value>` |
| `/pattern/` | Regex | Apply POSIX Extended Regular Expression; if a capture group exists, return the first capture; otherwise return the full match |
| `-` | Explicit none | Same as empty — publish the full payload without extraction |

See [Key Extraction](#key-extraction) for detailed documentation.

### Parameter: interval

The `interval` parameter specifies how often (in seconds) the metric value is published.

- For file and shell sources: `interval` is **required** and must be greater than 0.
- For `$`-sources (remote brokers): `interval` is **optional**:
  - **Without `interval`**: event-driven mode — values are published immediately when received from the remote broker.
  - **With `interval`**: timer mode — the last received value is republished at the specified interval, even if no new data has arrived.

The interval sleep is implemented incrementally: 100 cycles of 100 ms each, allowing the thread to respond to a shutdown signal within ~100 ms instead of waiting for the full interval.

### Parameter: qos

The `qos` parameter sets the MQTT Quality of Service level for the metric:

| Value | Name | Behavior |
|-------|------|----------|
| `0` | At most once | Fire-and-forget. No acknowledgment. Lowest overhead. **Default.** |
| `1` | At least once | Guaranteed delivery, may be duplicated. Requires PUBACK. |
| `2` | Exactly once | Guaranteed single delivery. Two-phase handshake. Highest overhead. |

See [QoS Levels](#qos-levels) for details.

### Parameter: topic

The MQTT topic to publish the metric value to. Must be unique within the configuration file.

Topic naming recommendations:
- Use forward slashes `/` to create a hierarchical structure: `sensors/rpi/cpu_temp`
- Start with a category: `sensors/`, `system/`, `home/`
- Include device identifier for multi-device setups: `sensors/rpi4-living/cpu_temp`
- Keep topics short but descriptive

---

## Source Types

### File Sources

If `source` does not start with `@` or `$`, the value is treated as a file path. The program opens the file with `open()`, reads its content with a single `read()` call (up to 8191 bytes), and uses the result as the metric value.

```ini
# Read CPU temperature from sysfs (Raspberry Pi)
source=/sys/class/thermal/thermal_zone0/temp

# Read memory info from procfs
source=/proc/meminfo

# Read DHT sensor value from procfs
source=/proc/sensors/dht/gpio4/value
```

#### procfs and sysfs Compatibility

File sources use `open()` + `read()` instead of `fopen()` + `fread()`. This is critical for procfs and sysfs compatibility:

- **procfs** (`/proc/...`): Pseudo-files generated on-demand by the kernel. `fread()` may block or return partial data due to stdio buffering. A single `read()` call returns the complete content.
- **sysfs** (`/sys/...`): Similar to procfs — kernel-generated content. The `open()`+`read()` approach ensures reliable reads.
- **Regular files**: Work equally well with both approaches.

The buffer size for file reads is `MAX_RAW` (8192 bytes). If the file content exceeds this limit, only the first 8191 bytes are read.

#### Error Handling

- File does not exist or is not readable → error logged, publish skipped.
- `read()` returns an error (not empty) → error logged, publish skipped.
- Trailing whitespace and newlines are always trimmed from the value.

### Shell Command Sources

If `source` starts with `@`, the remainder is executed as a shell command via `popen()`. The program reads the command's stdout and uses it as the metric value.

```ini
# CPU temperature via vcgencmd (Raspberry Pi)
source=@vcgencmd measure_temp | awk -F= '{print $2}'

# Free memory in MB
source=@free -m | awk '/^Mem:/ {print $4}'

# 1-minute load average
source=@uptime | awk '{print $10}' | tr -d ','
```

#### How popen() Works

`popen()` invokes `/bin/sh -c "<command>"`, so the full shell syntax is available: pipes, redirects, command substitution, variables, etc.

The command's stdout is read with `fread()` up to `MAX_RAW` (8192) bytes. The `pclose()` call checks the exit status:
- **Exit code 0**: data is published normally.
- **Non-zero exit code**: a `[warn]` message is logged in debug mode, but data is **not discarded** — the command may have produced useful output before failing.

#### Error Handling

- `popen()` fails (cannot start shell) → error logged, publish skipped.
- `fread()` I/O error (not empty output) → error logged, publish skipped.
- Non-zero exit code → warning in debug mode, data preserved.
- Trailing whitespace and newlines are always trimmed.

### Remote Broker Sources

If `source` starts with `$`, the program subscribes to a remote MQTT broker topic and republishes received messages to the local broker.

#### Syntax

```
$[user:pass@]host:port/topic
```

| Component | Required | Description |
|----------|----------|-------------|
| `user:pass@` | No | Authentication credentials for the remote broker |
| `host` | Yes | Remote broker hostname or IP (letters, digits, `.`, `-` only) |
| `port` | No | Remote broker port (default: 1883) |
| `topic` | Yes | Remote topic to subscribe to (everything after the first `/`) |

#### Examples

```ini
# Remote sensor data without auth
source=$192.168.1.50:1883/sensors/temperature

# Remote with authentication
source=$user:pass@remote.broker.com:1883/floor1/humidity

# Remote on default port (1883)
source=$broker.local/data/room_temp
```

See [Remote Broker Bridging](#remote-broker-bridging) for detailed architecture documentation.

---

## Key Extraction

The `key` parameter processes the raw source content before publishing. This is especially useful when a source returns multi-line data and you need a specific value.

### Literal Key

When `key` is a plain string (not enclosed in `/.../` and not `-`), the program searches the source content line-by-line for `key=<value>` and publishes `<value>`.

```ini
# /proc/sensors/dht/gpio4/value returns:
# H=45.2
# T=23.1
#
# Extract temperature:
key=T

# Extract humidity:
key=H
```

The search is line-based: the program looks for a line that starts with the key followed by `=`. The value is everything after `=` up to the end of the line. Leading/trailing whitespace is trimmed.

### Regex Key

When `key` is enclosed in `/.../`, the content between the slashes is treated as a POSIX Extended Regular Expression (ERE). The regex is applied to the entire raw source content with `REG_EXTENDED | REG_NEWLINE` flags.

- **If the pattern contains a capture group `()`**, the first captured group is returned.
- **If no capture group is present**, the full match is returned.
- The result is trimmed of leading/trailing whitespace.

```ini
# Extract numeric temperature from "temp=52.3'C"
key=/^temp=([0-9]+\.?[0-9]*)/

# Extract humidity value from "H=45.2"
key=/^H=([0-9]+\.?[0-9]*)/

# Match any line containing "temp=" and return the full match
key=/temp=[0-9]+\.?[0-9]*/

# Extract the first floating-point number from any line
key=/([0-9]+\.[0-9]+)/
```

#### Regex Flags

| Flag | Meaning |
|------|---------|
| `REG_EXTENDED` | POSIX Extended Regular Expression syntax (ERE) — supports `+`, `?`, `|`, `{n,m}`, `()`, `[]` |
| `REG_NEWLINE` | `^` and `$` match at the beginning/end of each line, not just the entire string; `.` does not match newline |

### No Extraction (key=-)

Setting `key=-` explicitly disables key extraction. The entire raw source content is trimmed and published as-is. This is equivalent to omitting the `key` parameter.

```ini
# Publish the entire file content
source=/proc/sensors/dht/gpio4/value
key=-
```

Use `key=-` when you want to make it explicit that the full payload should be published, for readability.

### Regex Reference

The program uses POSIX Extended Regular Expressions (ERE). Here is a quick reference:

#### Metacharacters

| Char | Meaning |
|------|---------|
| `.` | Any character except newline |
| `*` | Zero or more of preceding |
| `+` | One or more of preceding |
| `?` | Zero or one of preceding |
| `^` | Start of line (with `REG_NEWLINE`) |
| `$` | End of line (with `REG_NEWLINE`) |
| `|` | Alternation (OR) |
| `[]` | Character class |
| `()` | Capture group |
| `{n}` | Exactly n occurrences |
| `{n,m}` | Between n and m occurrences |
| `\` | Escape next character |

#### Common Patterns

| Pattern | Matches |
|---------|---------|
| `[0-9]+` | Integer |
| `[0-9]+\.?[0-9]*` | Decimal number (e.g., `23.1`, `45`, `0.5`) |
| `-?[0-9]+\.?[0-9]*` | Signed decimal (e.g., `-5.3`, `23.1`) |
| `[0-9]+\.[0-9]+` | Strict decimal (e.g., `23.1`, not `45`) |
| `[a-zA-Z]+` | Alphabetic string |
| `[a-zA-Z0-9]+` | Alphanumeric string |
| `^([0-9]+)` | Number at start of line |

#### Practical Examples

```ini
# CPU temp from /sys/class/thermal/thermal_zone0/temp (returns millidegrees)
# e.g., "48312" -> publish "48312" (divide in the consumer)
source=/sys/class/thermal/thermal_zone0/temp
key=-

# CPU temp from vcgencmd output: "temp=52.3'C"
# Extract just the number
source=@vcgencmd measure_temp
key=/temp=([0-9]+\.?[0-9]*)/

# DHT sensor value: "H=45.2\nT=23.1\n"
# Extract temperature
source=/proc/sensors/dht/gpio4/value
key=T

# Same, using regex
source=/proc/sensors/dht/gpio4/value
key=/^T=([0-9]+\.?[0-9]*)$/

# Free memory from /proc/meminfo: "MemFree:        123456 kB"
# Extract the number
source=/proc/meminfo
key=/^MemFree:\s+([0-9]+)/

# Load average from uptime: "...load average: 0.15, 0.20, 0.10"
# Extract 1-minute value
source=@uptime
key=/load average:\s+([0-9]+\.[0-9]+)/
```

---

## procfs Compatibility

### How procfs Differs

procfs (`/proc`) and sysfs (`/sys`) are pseudo-filesystems: their files are not stored on disk but generated on-the-fly by the kernel. This has important implications for how they should be read:

| Aspect | Regular file | procfs/sysfs file |
|--------|-------------|-------------------|
| Data persistence | Stored on disk | Generated on each read |
| `fread()` behavior | Reads entire file in one call | May return partial data or block due to stdio buffering |
| `read()` behavior | Reads available bytes | Returns complete content in one call |
| Seeking | Works | May not work as expected |
| File size | Known in advance | Unknown (stat may report 0) |

MQTT Publisher uses `open()` + `read()` for file sources specifically to handle procfs and sysfs correctly. Shell command sources (`@`-prefix) use `popen()` + `fread()`, which is fine for command output since the shell handles buffering.

### DHT Driver Integration

The [DHT driver](https://github.com/chapvic/dht-driver) is a Linux kernel module for reading DHT11/DHT22/AM2302 temperature and humidity sensors on Raspberry Pi GPIO pins. It exposes sensor data through procfs.

#### procfs Hierarchy

```
/proc/sensors/dht/
├── debug           (rw) — debug logging: 0 = off, 1 = on
├── version         (r)  — driver version
├── export          (w)  — write BCM pin number to register a sensor
├── unexport        (w)  — write BCM pin number to unregister a sensor
├── auto_interval   (rw) — global auto-poll interval (2–60, -1 = off)
└── gpio<pin>/
    ├── pin           (r)  — BCM GPIO pin number
    ├── interval      (rw) — per-sensor auto-poll interval (2–60, -1 = off)
    ├── measure       (w)  — write "1" to trigger measurement
    ├── status_code   (r)  — error code (0 = success)
    ├── status_text   (r)  — error description
    ├── value         (r)  — "H=<humidity>\nT=<temperature>\n"
    ├── info          (r)  — sensor type + registration time
    └── timestamp     (r)  — Unix timestamp of last measurement
```

#### value File Format

The `value` file outputs two lines:

```
H=45.2
T=23.1
```

- `H=` — humidity in percent with one decimal place
- `T=` — temperature in degrees Celsius with one decimal place; negative values prefixed with `-`

Example with negative temperature:

```
H=52.0
T=-5.3
```

#### Configuring MQTT Publisher with DHT Sensors

```ini
# DHT22 sensor on GPIO4 — temperature
[dht_temp]
topic=sensors/dht22/living_room/temperature
interval=10
source=/proc/sensors/dht/gpio4/value
key=T
qos=1

# DHT22 sensor on GPIO4 — humidity
[dht_hum]
topic=sensors/dht22/living_room/humidity
interval=10
source=/proc/sensors/dht/gpio4/value
key=H
qos=1

# Same sensor using regex extraction
[dht_temp_regex]
topic=sensors/dht22/living_room/temperature
interval=10
source=/proc/sensors/dht/gpio4/value
key=/^T=(-?[0-9]+\.?[0-9]*)$/
qos=1
```

#### DHT Driver Setup

1. **Build and load the driver** (see the [DHT driver repository](https://github.com/chapvic/dht-driver) for details):

```bash
# Build
make
sudo insmod dht.ko

# Register a sensor on GPIO4
echo 4 | sudo tee /proc/sensors/dht/export

# Enable auto-polling every 10 seconds
echo 10 | sudo tee /proc/sensors/dht/auto_interval

# Verify
cat /proc/sensors/dht/gpio4/value
# Output: H=45.2\nT=23.1
```

2. **Auto-registration at boot** — create `/etc/default/dht`:

```
# DHT driver configuration
AUTO_INTERVAL=10
SENSOR=4
SENSOR=17,5
```

3. **Configure MQTT Publisher** — add the DHT metrics to your `mqtt_pub.ini` as shown above.

---

## Raspberry Pi Metrics Catalog

This section provides a catalog of common Raspberry Pi metrics and how to configure them in MQTT Publisher.

### Temperature

#### CPU Temperature (sysfs)

The CPU (SoC) temperature is available in millidegrees Celsius via sysfs:

```ini
[cpu_temp_raw]
topic=sensors/rpi/cpu_temp_raw
interval=5
source=/sys/class/thermal/thermal_zone0/temp
key=-
qos=0
```

Output: `48312` (millidegrees; divide by 1000 in the consumer to get 48.3°C).

#### CPU Temperature (vcgencmd)

Using the Raspberry Pi `vcgencmd` utility, which outputs `temp=52.3'C`:

```ini
[cpu_temp]
topic=sensors/rpi/cpu_temp
interval=5
source=@vcgencmd measure_temp
key=/temp=([0-9]+\.?[0-9]*)/
qos=1
```

Output: `52.3` (degrees Celsius).

#### GPU Temperature

On Raspberry Pi, the GPU shares the SoC with the CPU, so the temperature is the same. However, you can use `vcgencmd` for a dedicated reading:

```ini
[gpu_temp]
topic=sensors/rpi/gpu_temp
interval=10
source=@vcgencmd measure_temp | awk -F= '{print $2}' | tr -d "'C"
qos=0
```

### Memory

#### Free Memory (procfs with key extraction)

`/proc/meminfo` contains many lines; extract specific values:

```ini
[mem_free]
topic=sensors/rpi/mem_free_kb
interval=10
source=/proc/meminfo
key=/^MemFree:\s+([0-9]+)/
qos=0
```

Output: `123456` (kB).

#### Free Memory (shell command)

Using `free` command for a more readable format:

```ini
[mem_free_mb]
topic=sensors/rpi/mem_free_mb
interval=10
source=@free -m | awk '/^Mem:/ {print $4}'
qos=0
```

Output: `456` (MB).

#### Available Memory

```ini
[mem_available]
topic=sensors/rpi/mem_available_kb
interval=10
source=/proc/meminfo
key=/^MemAvailable:\s+([0-9]+)/
qos=0
```

#### Total Memory

```ini
[mem_total]
topic=sensors/rpi/mem_total_kb
interval=60
source=/proc/meminfo
key=/^MemTotal:\s+([0-9]+)/
qos=0
```

### CPU Load

#### 1-Minute Load Average

```ini
[load_avg_1m]
topic=sensors/rpi/load_avg_1m
interval=15
source=@uptime | awk '{print $10}' | tr -d ','
qos=0
```

#### Load Average from /proc/loadavg

```ini
[load_avg_1m_proc]
topic=sensors/rpi/load_avg_1m
interval=15
source=/proc/loadavg
key=/^([0-9]+\.[0-9]+)/
qos=0
```

Output: `0.15` (1-minute load average).

#### CPU Usage Percentage

```ini
[cpu_usage]
topic=sensors/rpi/cpu_usage
interval=10
source=@top -bn1 | awk '/^%Cpu/ {print $2}' | head -1
qos=0
```

### Disk and Filesystem

#### Root Partition Usage

```ini
[disk_root_used]
topic=sensors/rpi/disk_root_used_pct
interval=60
source=@df -h / | awk 'NR==2 {print $5}' | tr -d '%'
qos=0
```

Output: `45` (percent used).

#### Root Partition Free Space

```ini
[disk_root_free]
topic=sensors/rpi/disk_root_free_gb
interval=60
source=@df -h / | awk 'NR==2 {print $4}'
qos=0
```

#### SD Card Write Operations

```ini
[disk_writes]
topic=sensors/rpi/disk_ops_write
interval=30
source=/proc/diskstats
key=/^sda\s+.*\s+([0-9]+)\s+[0-9]+\s+[0-9]+$/
qos=0
```

### Network

#### Interface RX Bytes

```ini
[net_rx_eth0]
topic=sensors/rpi/net_rx_eth0
interval=10
source=/proc/net/dev
key=/eth0:\s+([0-9]+)/
qos=0
```

#### Interface TX Bytes

```ini
[net_tx_eth0]
topic=sensors/rpi/net_tx_eth0
interval=10
source=@cat /proc/net/dev | awk '/eth0/ {print $10}'
qos=0
```

#### WLAN Signal Level

```ini
[wlan_signal]
topic=sensors/rpi/wlan_signal
interval=30
source=@iwconfig wlan0 | grep -o 'Signal level=[0-9-]*' | awk -F= '{print $2}'
qos=0
```

### Clock Speeds and Voltages

#### ARM Clock Frequency

```ini
[arm_freq]
topic=sensors/rpi/arm_freq
interval=10
source=@vcgencmd measure_clock arm | awk -F= '{print $2}'
qos=0
```

Output: `1500004352` (Hz).

#### Core Voltage

```ini
[core_voltage]
topic=sensors/rpi/core_voltage
interval=30
source=@vcgencmd measure_volts core | awk -F= '{print $2}' | tr -d 'V'
qos=0
```

Output: `1.2000` (volts).

### Throttling Status

The `vcgencmd get_throttled` command returns a hexadecimal bit pattern indicating whether the Pi has been throttled (under-voltage or thermal):

```ini
[throttled]
topic=sensors/rpi/throttled
interval=30
source=@vcgencmd get_throttled | awk -F= '{print $2}'
qos=0
```

Output: `0x0` (no throttling) or `0x50000` (past under-voltage).

### DHT Sensor Data

Using the [DHT driver](https://github.com/chapvic/dht-driver):

```ini
# Temperature from DHT22 on GPIO4
[dht_temp_living]
topic=sensors/dht/living/temperature
interval=10
source=/proc/sensors/dht/gpio4/value
key=T
qos=1

# Humidity from DHT22 on GPIO4
[dht_humidity_living]
topic=sensors/dht/living/humidity
interval=10
source=/proc/sensors/dht/gpio4/value
key=H
qos=1

# Temperature from DHT11 on GPIO17
[dht_temp_kitchen]
topic=sensors/dht/kitchen/temperature
interval=15
source=/proc/sensors/dht/gpio17/value
key=T
qos=1

# Humidity from DHT11 on GPIO17
[dht_humidity_kitchen]
topic=sensors/dht/kitchen/humidity
interval=15
source=/proc/sensors/dht/gpio17/value
key=H
qos=1
```

---

## Shell Command Optimization

Shell commands are powerful but can be expensive on resource-constrained devices like Raspberry Pi. Each `@`-source invocation spawns a shell process, which has overhead. Follow these guidelines to minimize impact:

### Prefer File Sources When Possible

If a metric is available as a file in procfs or sysfs, use a file source instead of a shell command. File reads are significantly faster — no process spawn, no shell interpretation.

```ini
# GOOD: file source (fast)
source=/sys/class/thermal/thermal_zone0/temp

# AVOID: shell command for the same data (slower)
source=@cat /sys/class/thermal/thermal_zone0/temp
```

### Minimize Process Count

Each `popen()` call runs `/bin/sh -c "<command>"`, which may spawn additional processes for pipes. A command like `@free -m | awk '/^Mem:/ {print $4}'` creates three processes: `sh`, `free`, and `awk`.

```ini
# Fewer processes: awk reads /proc/meminfo directly
source=@awk '/^MemFree:/ {print $2}' /proc/meminfo

# Even better: use file source with key extraction (no processes at all)
source=/proc/meminfo
key=/^MemFree:\s+([0-9]+)/
```

### Choose Appropriate Intervals

Not all metrics need to be read every second. Match the interval to the rate of change:

| Metric Type | Recommended Interval |
|-------------|----------------------|
| Temperature | 5–15 seconds |
| Humidity | 10–30 seconds |
| Memory usage | 10–60 seconds |
| Disk usage | 60–300 seconds |
| Network stats | 5–30 seconds |
| Clock speeds | 10–60 seconds |
| DHT sensors | 10–30 seconds (minimum 2s between reads) |

### Avoid Heavy Commands

Some commands are computationally expensive on Raspberry Pi:

| Command | Issue | Alternative |
|---------|-------|-------------|
| `top -bn1` | Spawns top, reads /proc multiple times | Use `/proc/stat` or `/proc/loadavg` |
| `ps aux` | Lists all processes | Use specific `/proc/<pid>/` files |
| `find /` | Traverses entire filesystem | Use known paths |
| `du -sh /` | Scans entire directory tree | Use `df` for partition stats |

### Use `key=` to Avoid Piping

Instead of using shell pipes to extract a value, read the raw file and use the `key=` parameter:

```ini
# SLOWER: spawns grep + awk
source=@grep MemFree /proc/meminfo | awk '{print $2}'

# FASTER: file read + key extraction (no processes)
source=/proc/meminfo
key=/^MemFree:\s+([0-9]+)/
```

### Benchmarking Shell Commands

To check the overhead of a shell command, measure its execution time:

```bash
time vcgencmd measure_temp
time free -m | awk '/^Mem:/ {print $4}'
time awk '/^MemFree:/ {print $2}' /proc/meminfo
```

On a Raspberry Pi 4, a simple shell command takes 5–20 ms. With 20 metrics at 10-second intervals, the total shell overhead is about 0.1–0.4 seconds per cycle — negligible, but worth monitoring.

---

## Remote Broker Bridging

### Architecture

When `source` starts with `$`, the program creates a separate Mosquitto client instance for each unique remote broker (deduplicated by `host:port`). The client subscribes to the specified remote topic and republishes received messages to the local broker.

```
Remote Broker A          Local Broker
  topic: sensors/temp       topic: sensors/rpi/remote_temp
       │                          │
       └── $remote_A:1883/sensors/temp
              │
         mqtt_pub (remote client)
              │
         mqtt_pub (local client)
              │
         local broker:1883
```

Multiple metrics from the same remote broker share a single connection. Up to `MAX_REMOTES` (32) remote brokers are supported.

### Event-Driven vs Timer Mode

#### Event-Driven (no `interval`)

When `interval` is omitted for a `$`-source, the metric is published immediately when a message arrives from the remote broker. This provides the lowest latency.

```ini
[remote_temp]
topic=sensors/rpi/remote_temp
source=$192.168.1.50:1883/sensors/temp
qos=1
# No interval = event-driven
```

#### Timer Mode (with `interval`)

When `interval` is specified for a `$`-source, a timer thread republishes the last known value at the specified interval. If no message has been received yet, nothing is published. Once a message arrives, its value is cached and republished on schedule.

```ini
[remote_temp_buffered]
topic=sensors/rpi/remote_temp
source=$192.168.1.50:1883/sensors/temp
interval=30
qos=1
# interval=30 = republish last value every 30 seconds
```

Timer mode is useful when the remote source publishes infrequently or irregularly, and you need a regular heartbeat for your monitoring system.

### Shared Connections

When multiple metrics subscribe to the same remote broker (same `host:port`), they share a single Mosquitto connection. The program deduplicates connections by comparing `host:port` pairs:

```ini
# Both metrics use the same remote broker (192.168.1.50:1883)
# Only one connection is created
[remote_temp]
source=$192.168.1.50:1883/sensors/temp

[remote_humidity]
source=$192.168.1.50:1883/sensors/humidity
```

On reconnection (after a network failure), the program automatically re-subscribes to all topics on that broker.

### Auto-Reconnect

Remote broker connections use `mosquitto_reconnect_delay_set()` with:
- Initial delay: 1 second
- Maximum delay: 60 seconds
- No exponential backoff (linear retry)

This means a disconnected remote broker is retried every 1 second initially, up to once per 60 seconds. When the broker comes back online, the program logs `[remote] <host>:<port> back online` and re-subscribes to all topics.

### Fault Tolerance

Remote broker setup is **non-fatal**:
- If a remote broker is unreachable at startup, the program logs a warning and continues. Local metrics are not affected.
- If a remote broker goes down during operation, local metrics continue publishing normally.
- If a remote broker's configuration is invalid (bad host, no topic), the metric is skipped with an error message.

---

## How It Works

### Lifecycle

1. **Argument parsing** — reads `-f` and `-h` options, determines the config file path.
2. **Configuration loading** — the INI file is parsed and validated. Broker settings and metric definitions are populated into global structures.
3. **Signal handler installation** — `SIGINT` and `SIGTERM` are intercepted for graceful shutdown.
4. **Mosquitto initialization** — `mosquitto_lib_init()`, client instance created, credentials set if authentication is configured.
5. **Callback registration** — `on_connect` and `on_disconnect` callbacks are registered for logging.
6. **Broker connection** — `mosquitto_connect()` with retry loop: if the broker is unreachable, the program retries every 1 second with single-error logging. After successful connect, CONNACK is polled every 100 ms (up to 2 seconds).
7. **Network loop start** — `mosquitto_loop_start()` launches a background thread for network event processing and auto-reconnect.
8. **Local metric threads** — for each file/shell metric, a `metric_thread` is created.
9. **Remote broker setup** — `setup_remote_brokers()` creates remote connections (non-blocking, non-fatal). For each remote metric with `interval`, a `remote_timer_thread` is also started.
10. **Main loop** — the main thread sleeps in 1-second cycles, checking `g_running`.
11. **Shutdown** — `cleanup_all()`: join all metric threads, stop remote broker loops, disconnect and destroy all Mosquitto instances, cleanup library.

### Thread Model

| Thread | Count | Purpose |
|--------|-------|---------|
| Main thread | 1 | Setup, main loop, signal handling |
| Mosquitto loop (local) | 1 | Network events for local broker |
| Mosquitto loop (per remote) | 1 per remote broker | Network events for each remote broker |
| `metric_thread` | 1 per local metric | Read source and publish at interval |
| `remote_timer_thread` | 1 per remote metric with `interval` | Republish last value at interval |

### Thread Safety

- **`g_mqtt_mutex`** (global) — protects `mosquitto_publish()` calls. Only one thread publishes to the local broker at a time.
- **`value_mutex`** (per metric) — protects `last_value` and `has_last_value` fields on remote metrics, preventing races between `remote_on_message` and `remote_timer_thread`.
- **`g_running`** (sig_atomic_t) — safe to read from any thread and write from signal handler.
- **`g_mqtt_connected`** (sig_atomic_t) — checked by local metric threads before attempting to publish.

### Connection Callbacks

#### Local Broker

- **`on_connect`** — sets `g_mqtt_connected` to `1` (success) or `-1` (CONNACK refused). Logs `[mqtt] connected to broker` in debug mode.
- **`on_disconnect`** — resets `g_mqtt_connected` to `0`. Logs unexpected disconnect only if previously connected and the program is still running.

#### Remote Brokers

- **`remote_on_connect`** — sets `connected = true`, re-subscribes to all metrics on this broker. Logs initial connection and reconnections.
- **`remote_on_disconnect`** — sets `connected = false`. Logs disconnect once (no spam during retries).
- **`remote_on_message`** — matches incoming messages to metric definitions by topic, applies key extraction, stores `last_value`, and immediately publishes to the local broker.

---

## Signal Handling

| Signal | Behavior |
|--------|----------|
| `SIGINT` (Ctrl+C) | Sets `g_running = 0`, initiates graceful shutdown |
| `SIGTERM` | Sets `g_running = 0`, initiates graceful shutdown |

### Shutdown Sequence

1. `g_running` is set to `0`.
2. All metric threads exit their loops within ~100 ms (incremental sleep).
3. `cleanup_all()` performs ordered teardown:
   - `pthread_join` all local metric threads
   - `pthread_join` all remote timer threads
   - Destroy per-metric `value_mutex`
   - `mosquitto_loop_stop(true)` for local and all remote brokers
   - `mosquitto_disconnect` + `mosquitto_destroy` for all instances
   - `mosquitto_lib_cleanup()`
   - `pthread_mutex_destroy(&g_mqtt_mutex)`
4. Program prints `Done.` and exits with code 0.

---

## QoS Levels

| QoS | Name | Behavior | Overhead | Use Case |
|-----|------|----------|----------|----------|
| `0` | At most once | Fire-and-forget, no acknowledgment | Lowest | Non-critical metrics where occasional loss is acceptable |
| `1` | At least once | Guaranteed delivery, may duplicate | Medium | Important metrics where loss is unacceptable |
| `2` | Exactly once | Guaranteed single delivery | Highest | Critical metrics where duplicates cause problems |

> **Note:** QoS 1 and 2 require broker support. With QoS 0, messages are lost if the broker is unavailable at publish time. Metric threads check `g_mqtt_connected == 1` before publishing, so messages are simply skipped (not queued) when the broker is down.

**Recommendations:**
- Use QoS 0 for high-frequency, low-value metrics (load average, network stats).
- Use QoS 1 for sensor data where losing a reading is undesirable (temperature, humidity).
- Use QoS 2 sparingly — the overhead is significant on constrained devices.

---

## Debug Mode

Debug mode is enabled by setting `debug=1` in the `[MQTT]` section.

### Quiet Mode (`debug=0`, default)

At startup:
- Program header (name, version, license, author)
- Metric list: section name and topic
- `Broker`, `Auth`, `Debug`, `Metrics` summary

During operation:
- Source read errors and publish errors → stderr

### Debug Mode (`debug=1`)

Additionally:
- Per-metric startup info: `topic`, `interval`, `source`, `qos`, `key`
- Every successful publish: `[pub] <topic> = <value>`
- Publish failures: `[err] publish to <topic> failed: <error>`
- Source read failures: `[err] failed to read source for [<section>]`
- Key not found: `[err] key '<key>' not found in '<source>'`
- Regex not found: `[err] regex '<pattern>' not found in '<source>'`
- Connection events: `[mqtt] connected to broker` / `[mqtt] connect failed: <error>`
- Disconnection events: `[mqtt] disconnected from broker` / `[mqtt] unexpected disconnect: <error>`
- Remote broker events: `[remote] <host>:<port> connected` / `[remote] <host>:<port> back online`
- Remote subscribe events: `[remote] subscribe <host> -> <topic> (rc=<code>)`
- Shell command warnings: `[warn] command exited with code <N> for source '<source>'`
- `Running... (Ctrl+C to stop)` message

---

## Buffer Limits

All buffer limits are defined as constants in the source code. Exceeding a limit causes a startup error with a descriptive message.

| Constant | Value | Purpose |
|----------|-------|---------|
| `MAX_VALUE` | 256 | Maximum size of a metric value returned by the source |
| `MAX_SHELL_CMD` | 2048 | Maximum length of a shell command or remote source in `source` |
| `MAX_SECTION` | 64 | Maximum length of a section name |
| `MAX_TOPIC` | 512 | Maximum length of an MQTT topic |
| `MAX_LINE` | 4096 | Maximum length of a line in the configuration file |
| `MAX_METRICS` | 128 | Maximum number of metric sections |
| `MAX_HOST` | 256 | Maximum length of a broker host address |
| `MAX_USER` | 64 | Maximum length of a username |
| `MAX_PASS` | 256 | Maximum length of a password |
| `MAX_KEY` | 64 | Maximum length of a key pattern (literal or regex) |
| `MAX_RAW` | 8192 | Maximum raw buffer for file reads and remote messages |
| `MAX_REMOTE_TOPIC` | 512 | Maximum length of a remote broker topic |
| `MAX_REMOTES` | 32 | Maximum number of remote broker connections |

---

## Validation Rules

The configuration file is fully validated at load time. Any error causes the program to print a message to stderr and exit with code 1.

| Check | Error Message |
|-------|--------------|
| `[MQTT]` is not the first section | `Error: [MQTT] must be the first section` |
| Duplicate `[MQTT]` section | `Error: duplicate section [MQTT]` |
| Duplicate metric section name | `Error: duplicate section [<name>]` |
| Section name exceeds 64 bytes | `Error: section name exceeds 64 bytes: [<name>]` |
| Topic exceeds 512 bytes | `Error: topic exceeds 512 bytes: <topic>` |
| Duplicate topic | `Error: duplicate topic '<topic>'` |
| Host exceeds 256 bytes | `Error: host exceeds 256 bytes: <host>` |
| User exceeds 64 bytes | `Error: user exceeds 64 bytes: <user>` |
| Password exceeds 256 bytes | `Error: password exceeds 256 bytes` |
| Source exceeds 2048 bytes | `Error: source exceeds 2048 bytes: <source>` |
| Port out of range | `Error: invalid port '<port>' (must be 1-65535)` |
| QoS out of range | `Error: invalid QoS <qos> for [<section>] (must be 0, 1, or 2)` |
| Key exceeds 64 bytes | `Error: key exceeds 64 bytes: <key>` |
| Too many metric sections | `Error: too many metric sections (max 128)` |
| Metric without `topic` | `Error: metric [<section>] has no topic` |
| Metric with invalid `interval` | `Error: metric [<section>] has invalid interval` |
| Metric without `source` | `Error: metric [<section>] has no source` |
| Cannot open config file | `Error: cannot open config file '<file>': <errno>` |
| Malformed section header | `Error: malformed section header at line <N>` |
| Malformed line (no `=`) | `Error: malformed line <N>: <line>` |

---

## Troubleshooting

### Program Exits at Startup

| Symptom | Cause | Solution |
|---------|-------|----------|
| `Error: cannot open config file` | File not found or no read permission | Check path with `-f`, verify permissions |
| `Error: [MQTT] must be the first section` | A metric section appears before `[MQTT]` | Move `[MQTT]` to the top of the file |
| `Error: duplicate section` | Two sections with the same name | Rename one of the sections |
| `Error: duplicate topic` | Two metrics publish to the same topic | Assign unique topics |
| `Error: too many metric sections` | More than 128 metrics | Split into multiple instances or reduce count |
| `Error: metric [<name>] has no source` | Missing `source=` parameter | Add `source=` to the section |

### No Data Published

| Symptom | Cause | Solution |
|---------|-------|----------|
| `[mqtt] connect failed` in logs | Broker not running or wrong address | Verify broker is running: `mosquitto_sub -h <host> -t '#'` |
| `[err] failed to read source` | File not found or command failed | Test the source manually |
| `[err] key not found` | Key does not match file content | Check the file content and key spelling |
| `[err] regex not found` | Regex does not match | Test with `echo "<data>" \| grep -E '<pattern>'` |
| `[warn] command exited with code` | Shell command failed but produced output | Check command syntax; data may still be valid |

### Remote Broker Issues

| Symptom | Cause | Solution |
|---------|-------|----------|
| `[remote] failed to parse source` | Invalid `$`-source syntax | Check format: `$[user:pass@]host:port/topic` |
| `[remote] <host>:<port> unavailable` | Remote broker not running | Verify remote broker is reachable |
| No remote data in local broker | Remote topic mismatch or no data on remote | Subscribe to remote topic directly to verify |
| `[remote] too many remote brokers` | More than 32 remote brokers | Consolidate or reduce remote sources |

### Service Won't Start

```bash
# Check service status
systemctl status mqtt_pub

# Check logs
journalctl -u mqtt_pub -e

# Check config file exists
ls -la /etc/mqtt_pub.ini

# Check env file
cat /etc/default/mqtt_pub

# Run manually to see output
mqtt_pub -f /etc/mqtt_pub.ini
```

---

## Best Practices

### Configuration

- **Group related metrics** — use consistent topic hierarchies: `sensors/<location>/<metric>`.
- **Set appropriate intervals** — don't poll faster than the source can update. DHT sensors need at least 2 seconds between reads.
- **Use QoS 1 for sensor data** — QoS 0 may lose messages; QoS 2 is rarely worth the overhead.
- **Enable debug during setup** — set `debug=1` while configuring, then switch to `0` for production.
- **Keep config readable** — add comments with `#` explaining each metric's purpose and source.

### Performance on Raspberry Pi

- **Prefer file sources** over shell commands — `open()+read()` is faster than `popen()`.
- **Minimize pipes in shell commands** — each `|` spawns an additional process.
- **Avoid `cat` in shell sources** — use file sources directly: `source=/proc/meminfo` instead of `source=@cat /proc/meminfo`.
- **Stagger intervals** — if you have 20 metrics, don't set all to `interval=5`. Mix 5s, 10s, 15s, 30s to spread the load.
- **Monitor total process count** — each shell metric adds 2+ processes per cycle. With 30 shell metrics at 5s intervals, that's 60+ process spawns per minute.

### Security

- **Use authentication** — always set `user` and `password` for brokers exposed beyond localhost.
- **Restrict config file permissions** — `chmod 640 /etc/mqtt_pub.ini` (set by Makefile during install).
- **Use TLS** — if your broker supports TLS, configure it in the broker (mosquitto.conf) and set the port accordingly.
- **Isolate on VPN** — for remote brokers, use VPN or private networks rather than exposing MQTT to the internet.

---

## Integration Examples

### Home Assistant

In Home Assistant, add MQTT sensor configurations:

```yaml
# configuration.yaml
sensor:
  - platform: mqtt
    name: "CPU Temperature"
    state_topic: "sensors/rpi/cpu_temp"
    unit_of_measurement: "°C"
    qos: 1

  - platform: mqtt
    name: "Living Room Humidity"
    state_topic: "sensors/dht/living/humidity"
    unit_of_measurement: "%"
    qos: 1

  - platform: mqtt
    name: "Free Memory"
    state_topic: "sensors/rpi/mem_free_mb"
    unit_of_measurement: "MB"
```

### Node-RED

In Node-RED, create an MQTT-in node subscribed to your metric topics and process the data:

```
[MQTT In: sensors/rpi/cpu_temp] → [Function: format] → [Dashboard: gauge]
```

### Multi-Raspberry Pi Setup

Use remote sources to aggregate metrics from multiple Pis to a central broker:

```ini
# On the central broker's mqtt_pub.ini:
[pi4_temp]
topic=sensors/pi4/cpu_temp
source=$pi4.local:1883/sensors/rpi/cpu_temp
qos=1

[pi3_temp]
topic=sensors/pi3/cpu_temp
source=$pi3.local:1883/sensors/rpi/cpu_temp
qos=1

# Event-driven: publish immediately when received
[pi4_dht_temp]
topic=sensors/pi4/dht/temperature
source=$pi4.local:1883/sensors/dht/living/temperature
qos=1
```

### Complete Configuration Example

```ini
# MQTT Publisher Configuration
# (c) 2026, Chapvic

[MQTT]
host=localhost
port=1883
user=monitor
password=secret
debug=0

# ── Temperature ──

[cpu_temp]
topic=sensors/rpi/cpu_temp
interval=5
source=@vcgencmd measure_temp
key=/temp=([0-9]+\.?[0-9]*)/
qos=1

# ── Memory ──

[mem_free]
topic=sensors/rpi/mem_free_kb
interval=10
source=/proc/meminfo
key=/^MemFree:\s+([0-9]+)/
qos=0

[mem_available]
topic=sensors/rpi/mem_available_kb
interval=10
source=/proc/meminfo
key=/^MemAvailable:\s+([0-9]+)/
qos=0

# ── CPU Load ──

[load_avg_1m]
topic=sensors/rpi/load_avg_1m
interval=15
source=/proc/loadavg
key=/^([0-9]+\.[0-9]+)/
qos=0

# ── Disk ──

[disk_root_used]
topic=sensors/rpi/disk_root_used_pct
interval=60
source=@df -h / | awk 'NR==2 {print $5}' | tr -d '%'
qos=0

# ── DHT22 Sensor on GPIO4 ──

[dht_temp_living]
topic=sensors/dht/living/temperature
interval=10
source=/proc/sensors/dht/gpio4/value
key=T
qos=1

[dht_humidity_living]
topic=sensors/dht/living/humidity
interval=10
source=/proc/sensors/dht/gpio4/value
key=H
qos=1

# ── Remote Broker (event-driven) ──

[remote_pi3_temp]
topic=sensors/pi3/cpu_temp
source=$pi3.local:1883/sensors/rpi/cpu_temp
qos=1

# ── Remote Broker (timer mode) ──

[remote_pi3_load]
topic=sensors/pi3/load_avg_1m
source=$pi3.local:1883/sensors/rpi/load_avg_1m
interval=30
qos=0
```

---

## License

```
MQTT Publisher
Copyright (c) 2026, Chapvic

This program is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <https://www.gnu.org/licenses/>.
```

---

*MQTT Publisher, v1.0 — (c) 2026, Chapvic — License: GPLv3*
