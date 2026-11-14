# iBORK-ESP32

A touchscreen dashboard for [iBORK](https://github.com/akira69/iBORK). The panel shows fleet power, energy history and per-server readings. It supports PIN-protected power controls on servers that advertise them.

## Build for the Elecrow CrowPanel 7.0 V3.0

The `crowpanel-v3` target is for the **DIS08070H V3.0** with an ESP32-S3, 800 × 480 RGB display, GT911 touch controller, 4 MB flash and octal PSRAM. Check the revision marked on your board before choosing this target.

Install PlatformIO Core, then build from this directory:

```sh
pio run -e crowpanel-v3
```

A fresh checkout builds an offline demo. On first boot, use **Back** from the network list and again from Settings to try the sample fleet. The demo PIN is `246810`. Demo actions affect the sample readings in memory.

Before uploading to this revision, identify its port with `pio device list`. Replace `PORT` below with that port. Use esptool 4.x to read and byte-verify a complete flash backup. Store it in a private directory because it may contain Wi-Fi and service credentials.

```sh
esptool.py --chip esp32s3 -p PORT read_flash 0 0x400000 backup.bin
esptool.py --chip esp32s3 -p PORT verify_flash 0 backup.bin
```

After verifying the backup, upload the build with `pio run -e crowpanel-v3 -t upload --upload-port PORT`. The serial monitor runs at 115200 baud.

## Pair with iBORK

1. Configure a dedicated token on your [iBORK server](https://github.com/akira69/iBORK/blob/main/docs/panel-api.md). You can enable read-only access or configure a separate panel PIN for controls.
2. Create the private header `include/panel_local.h` in your checkout. Create the `include` directory if needed. Define the macros below as C string literals. The firmware will embed these values, so keep the header, paired binaries and backups private.
3. Rebuild and upload. On the panel, open the settings gear beside Finch13, select **Wi-Fi setup**, choose a 2.4 GHz WPA2/WPA3 Personal network and enter its passphrase. The panel waits up to 25 seconds for an address and saves successful connections. A failed attempt restores the previous configuration.
4. Check the link indicators. The Wi-Fi icon turns green when the station connects. The service icon turns green after a fresh authenticated reading. The header adds text for demo, offline, stale or error states.

| Macro | Setting |
| --- | --- |
| `IBORK_PANEL_URL` | Required HTTPS origin. Its DNS name must appear on the server certificate. |
| `IBORK_PANEL_TOKEN` | Required 64-character hexadecimal bearer token from provisioning. |
| `IBORK_PANEL_CA` | Required PEM root CA used to verify the service certificate. |
| `IBORK_PANEL_IP` | Optional IPv4 address for routing to the service. TLS still verifies the hostname. |
| `IBORK_PANEL_NTP` | Optional reachable time server. Defaults to `pool.ntp.org`. Clock synchronization enables certificate validation. |
| `IBORK_PANEL_TZ` | Optional POSIX time zone for display schedules. Defaults to UTC. |

If you use a reverse proxy, route `/api/v1/panel` and its subpaths to iBORK and preserve the configured HTTPS origin. Rotate the server token and reprovision paired panels if a configured device is lost.

## Use the dashboard

While the screen is lit, the panel polls about every 15 seconds and refreshes chart history about once a minute. Opening a tile shows cached detail immediately and asks the server for an update. An open tile can trigger up to three quick follow-ups while its readings are pending. The dashboard spinner is reserved for the first load. Polling resumes on wake. When the home layout hides the tiles, use **Servers** in the header. A teal outline identifies each metered server even when its reading is missing.

Configure the home sections, server selection, detail widgets and electricity rate in iBORK. Charts leave gaps for unavailable readings. Costs use metered energy and the configured rate. To display custom icons, select **Copy sidebar icons to panel** in the dashboard's panel display settings. Other servers show their initials.

Open **Settings → Color** to compare Teal, Amber, Ice and Crimson. Tap a palette to save it on this display. The metered badge and server tiles share a teal outline in every mode.

Use **Settings → Display** to adjust this panel's brightness with the 10–100% slider. Manual brightness works offline and remains in effect after a restart until you tap **Use schedule**. Under **Schedule**, tap **Edit schedule** to set four daily brightness periods. In scheduled mode, touching a dimmed or sleeping screen wakes it; the schedule resumes after five minutes of inactivity. The panel caches the last saved schedule for outages. Paired panels require the current PIN to change the shared schedule.

On a supported server, tap the upper half of the knife switch for **Power on** or the lower half for **Graceful shutdown**, then enter the panel PIN to confirm. PIN entry expires after one minute of inactivity or when the display sleeps. The lever shows the latest available chassis state, labeled **Last known** when it is cached. If the outcome is unknown, check the server before trying again. **Settings → Change power PIN** changes the shared PIN for all paired panels.

## Board drivers and assets

The V3.0 board timing and touch initialization follow [Elecrow's reference implementation](https://github.com/Elecrow-RD/CrowPanel-7.0-HMI-ESP32-Display-800x480/tree/master/example/V3.0/PlatformIO70). To add an 800 × 480 landscape board, define its PlatformIO environment, implement its graphics device, touch events and backlight in `src/displays/`, and register the target in `src/panel.hpp`. A different resolution also needs a matching layout.

The iBORK and Finch13 marks come from the iBORK project. The Settings gear uses [Lucide artwork](art/LICENSE-lucide.txt). The firmware includes Inter Medium at 16, 25 and 40 px. Distribute the [font license](fonts/OFL.txt) with firmware binaries. To regenerate the fonts, run `python fonts/generate.py /path/to/Inter-Medium.ttf` with Pillow and Inter 3.19 installed.

## Host tests

On a POSIX development machine with a C++17 compiler, pkg-config, libcjson and Mbed TLS development files, run:

```sh
set -eu
out=$(mktemp -d)
trap 'rm -f "$out/test"; rmdir "$out"' EXIT
for source in tests/*_test.cpp; do
  c++ -std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined \
    -fno-omit-frame-pointer "$source" $(pkg-config --cflags --libs libcjson) \
    -lmbedcrypto -o "$out/test"
  "$out/test"
done
```

These tests exercise response parsing, timeout rollover, touch gestures, screen wake behavior and PIN changes using synthetic inputs.
