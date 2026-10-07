# MQTT Publisher v1.0

First release of **mqtt_pub** — a multi-threaded MQTT publisher for Linux embedded systems.

## Features

### Source types

- **File** — read value from a regular file via `open()`+`read()`
- **Shell command** (`@`-prefix) — execute via `popen()`, capture stdout
- **Remote broker** (`$`-prefix) — subscribe to a remote MQTT topic and republish to local broker

### Key extraction

| Mode | Syntax | Behavior |
|------|--------|----------|
| Full payload | `key=-` (default) | Publish entire source content |
| Literal key | `key=T` | Search for `T=<value>` line, publish `<value>` |
| Regex | `key=/^pattern$/` | POSIX ERE with capture group support |

### Remote metrics

- Format: `$[user:pass@]host:port/topic`
- Event-driven mode (no `interval`) — publish on each received message
- Timer republish mode (with `interval`) — republish last known value periodically
- Shared connections — deduplicate by `host:port` (up to 32 remote brokers)
- Auto-reconnect with bounded delay (1–60s, linear backoff)
- Fault tolerance — unreachable remote brokers don't block local metrics

### Core

- Multi-threaded — one thread per metric with independent interval
- QoS 0, 1, or 2 per metric
- Username/password authentication (local and remote brokers)
- Graceful shutdown on SIGINT/SIGTERM (100ms response time)
- Config validation at startup (duplicate sections, topics, invalid ports, buffer limits)
- Debug mode with verbose logging

## Installation

```bash
make
sudo make install
```

With parameters:

```bash
sudo make install HOST=192.168.1.10 PORT=1883 MQTT_USER=sensor MQTT_PASS=secret
```

### Local config file support

If `mqtt_pub.ini` exists in the project directory, it is copied to the
destination instead of being generated dynamically. This preserves
user-configured metrics. Command-line parameters (`HOST`, `PORT`,
`MQTT_USER`, `MQTT_PASS`) are still applied on top via `sed`.

| Priority | Condition | Behavior |
|----------|-----------|----------|
| 1 | `mqtt_pub.ini` in project dir | Copied to destination, overrides applied |
| 2 | No local, no destination config | Generated dynamically with defaults |
| 3 | No local, destination config exists | Only overrides updated, metrics preserved |

### Installed files

| File | Location |
|------|----------|
| Binary | `/usr/local/bin/mqtt_pub` |
| Systemd unit | `/etc/systemd/system/mqtt_pub.service` |
| Env file | `/etc/default/mqtt_pub` |
| Config | `/etc/mqtt_pub.ini` (default, overridable via `CONF=`) |

## Makefile targets

| Target | Description |
|--------|-------------|
| `make` | Build `mqtt_pub` |
| `make install` | Install all components |
| `make uninstall` | Remove all installed files |
| `make clean` | Remove binary |
| `make distclean` | Remove binary and `install.log` |

## Documentation

- [README.md](README.md) — quick start (English)
- [README_RU.md](README_RU.md) — quick start (Russian)
- `docs/MQTT_Publisher.md` — full documentation (English)
- `docs/MQTT_Publisher_RU.md` — full documentation (Russian)
- `mqtt_pub.ini.example` — example configuration with all modes

## Requirements

- GCC with C11 support
- libmosquitto (Mosquitto client library)
- pthread (POSIX threads)

## License

GPLv3 — (c) 2026, Chapvic
