# MQTT Publisher

A multi-threaded MQTT publisher for Linux that reads metrics from an INI config
file and publishes them to a local MQTT broker at specified intervals.

Supports three source types: files, shell commands, and remote MQTT broker
subscriptions with automatic republishing.

- **Author:** (c) 2026, Chapvic
- **License:** GPLv3
- **Version:** v1.0

---

## Features

- **Multi-threaded:** each metric runs in its own thread with an independent publish interval.
- **Three source types:** read from files, execute shell commands, or subscribe to remote MQTT brokers.
- **Key extraction:** extract a value from source content using a literal key (`key=T` searches for `T=<value>`) or a POSIX ERE regex (`key=/^pattern$/`).
- **Remote metrics:** subscribe to remote broker topics via `$[user:pass@]host:port/topic` and republish to the local broker. Event-driven (no interval) or timer-republish (with interval).
- **Shared remote connections:** multiple metrics on the same `host:port` share a single mosquitto instance.
- **Auto-reconnect:** both local and remote brokers automatically reconnect with bounded delay (1–60 seconds).
- **QoS support:** per-metric QoS 0, 1, or 2 (default 0).
- **Authentication:** optional username/password for both local and remote brokers.
- **Graceful shutdown:** SIGINT / SIGTERM triggers a clean shutdown — all threads are joined, connections are closed, resources are freed.
- **Config validation:** duplicate sections, duplicate topics, missing required fields, invalid ports, buffer overflows — all caught at startup.
- **Debug mode:** verbose logging of every published value, connection events, and source read errors.

---

## Requirements

- **GCC** or any C compiler with C11 support
- **pthread** library (part of glibc on Linux)
- **libmosquitto** — the Mosquitto MQTT client library

Install on Debian / Ubuntu:

```bash
sudo apt install build-essential libmosquitto-dev
```

Install on RHEL / Fedora:

```bash
sudo dnf install gcc mosquitto-devel
```

---

## Building

```bash
make
```

Or manually:

```bash
gcc -O2 -Wall -o mqtt_pub mqtt_pub.c -lmosquitto -lpthread
```

---

## Installation

```bash
sudo make install
```

With parameters:

```bash
sudo make install HOST=192.168.1.10 PORT=1883 MQTT_USER=sensor MQTT_PASS=secret
```

### Config handling during install

The install target follows this priority for the configuration file:

| Priority | Condition | Behavior |
|----------|-----------|----------|
| 1 | `mqtt_pub.ini` exists in project dir | Copied to destination. `HOST`, `PORT`, `MQTT_USER`, `MQTT_PASS` overrides applied on top via `sed`. |
| 2 | No local config, no config at destination | Generated dynamically with default values and any provided overrides. |
| 3 | No local config, config exists at destination | Only override parameters (`HOST`, `PORT`, `MQTT_USER`, `MQTT_PASS`) are updated. Existing metrics are preserved. |

### Install parameters

| Parameter | Default | Description |
|-----------|---------|-------------|
| `HOST` | `127.0.0.1` | Broker hostname or IP (applied to config) |
| `PORT` | `1883` | Broker port (applied to config) |
| `MQTT_USER` | *(empty)* | Username for auth (applied to config) |
| `MQTT_PASS` | *(empty)* | Password for auth (applied to config) |
| `CONF` | `/etc` | Destination directory for config file |

### What install does

1. Stops the `mqtt_pub` service if running.
2. Installs binary to `/usr/local/bin/mqtt_pub`.
3. Installs systemd unit to `/etc/systemd/system/mqtt_pub.service`.
4. Handles config file (see priority table above).
5. Creates/updates env file at `/etc/default/mqtt_pub` with `PARAMS=-f <config_path>`.
6. Saves `install.log` with the `CONF` path for future updates.
7. Reloads systemd, enables and starts the service.

---

## Uninstallation

```bash
sudo make uninstall
```

Removes the binary, systemd unit, env file, and config file. The `install.log` is preserved.

---

## Usage

```
Usage: mqtt_pub [-f <config_file>] [-h]
```

| Option | Description |
|--------|-------------|
| `-f <config_file>` | Full path to an INI config file. Defaults to `mqtt_pub.ini` in the current directory. |
| `-h` | Print help and exit. |

### Typical invocation

```bash
./mqtt_pub -f /etc/mqtt_pub.ini
```

---

## Configuration File

See `mqtt_pub.ini.example` for a complete example, and `docs/MQTT_Publisher.md` for full documentation.

### Quick reference

```ini
[MQTT]
host=localhost
port=1883
user=sensor
password=secret
debug=0

[cpu_temp]
topic=sensors/rpi/cpu_temp
interval=5
source=/sys/class/thermal/thermal_zone0/temp
key=-
qos=0

[remote_temp]
topic=sensors/weather/temp
source=$10.0.0.5:1883/sensors/weather/temp
key=-
qos=0
```

---

## Makefile targets

| Target | Description |
|--------|-------------|
| `make` | Build `mqtt_pub` |
| `make install` | Install binary, service, config, env file |
| `make uninstall` | Remove all installed files |
| `make clean` | Remove compiled binary |
| `make distclean` | Remove binary and `install.log` |

---

## License

GPLv3 — see the source file header for details.
