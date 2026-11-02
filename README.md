# Anybus ONE Fast SPI on a PC, through an MCP2210

This project runs the HMS **Anybus ONE Fast SPI host driver** in a console
application on a Windows PC. A Microchip **MCP2210** USB-to-SPI bridge acts as
the SPI master.

```
 PC (VS Code project)                      USB            MCP2210            SPI        Anybus ONE SoM
 console app (one_spi)                                     SCK  ───────────────────────── SCLK
   └─ abo_spi driver (HMS)                                 MOSI ───────────────────────── MOSI
        └─ port_mcp2210.c                                  MISO ───────────────────────── MISO
             └─ mcp2210.c ─ hidapi ─ Windows HID ─────────  GPx  ───────────────────────── CS
                                                            GND  ───────────────────────── GND
```

The HMS driver (`abo-driver-spi/`) and HMS's port header (`port/port.h`) are
copied unchanged. `--dump` reads the received and sent frames from the
handle's public `wireRx` and `wireTx` fields. The port (`Port_Open`, `Port_Close` and the two callbacks
`Transfer` and `NowUs`) is implemented in `port/port_mcp2210.c`.

## What you need

| | |
|---|---|
| VS Code | Extensions: **C/C++** and **CMake Tools**. VS Code offers both when you open the folder. |
| CMake ≥ 3.21 | Ships with Visual Studio. With MSYS2 you can also install it via `pacman`. |
| A C compiler | **Either** Visual Studio 2022 / Build Tools ("Desktop development with C++"), **or** MSYS2 UCRT64: `pacman -S mingw-w64-ucrt-x86_64-{gcc,gdb,ninja,cmake}` with `C:\msys64\ucrt64\bin` on `PATH`. |
| Git + internet on first configure | CMake downloads and builds **hidapi 0.15.0** as a static library. |
| No device driver | The MCP2210 is a USB HID device, so Windows' built-in driver is used. Microchip's DLL is not needed. |

## Getting started

1. Open this folder in VS Code. When CMake Tools asks for a **configure preset**,
   pick **MSVC x64** or **MinGW-w64 GCC + Ninja**.
2. **Build:** press `Ctrl+Shift+B`.
3. Plug in the MCP2210 and close Microchip's *MCP2210 Utility* if it is open.
   Run *Terminal → Run Task → List MCP2210 devices*.
4. **Run on hardware:** choose *Debug (MSVC)* or *Debug (MinGW gdb)* in the Run
   view and press `F5`, or run the task *Run one_spi*.

### Program output

The console app prints one status line per second:

```
   12.0s ONLINE    33 fr/s | usb  2.1/ 3.0/ 6.8 ms | acc=395 xfer=0 short=0 layout=0 crc=0 rep=0 idle=0 late=0 lost=0
```

- `fr/s` is the number of frames accepted in the last second.
- `usb` shows the min / average / max time of one MCP2210 transfer.
- The counters after it are the driver's nine counters. Send these with a
  support case.
- `--dump` also prints the last frame received on MISO (`to-host`) and the
  last frame sent on MOSI (`from-host`) in hex, header and CRC included. Both
  are the raw wire frames, so `to-host` shows what the ONE sent even when the
  driver rejected it.

## Command line

```
one_spi [--list] [--serial <sn>] [--cs <0..8>] [--bitrate <Hz>]
                [--vid <hex>] [--pid <hex>] [--seconds <n>] [--dump] [--verbose]
```

You can set the same options through environment variables (`MCP2210_SERIAL`,
`MCP2210_CS`, `MCP2210_BITRATE`, `MCP2210_VID`, `MCP2210_PID`) or as CMake cache
defaults (`ONE_MCP2210_CS_PIN`, `ONE_MCP2210_BITRATE_HZ`, `ONE_MCP2210_SERIAL`).

## Wiring

| ONE SoM | MCP2210 | Notes |
|---|---|---|
| SCLK | SCK | |
| MOSI | MOSI (SDO) | |
| MISO | MISO (SDI) | |
| CS | GP2 by default | Any GP0–GP8 pin works; choose it with `--cs` or `ONE_MCP2210_CS_PIN`. The port makes that pin a chip select in the MCP2210's **volatile** settings. NVRAM is never written. |
| GND | GND | Required. |

- **Logic levels:** the MCP2210 runs at its VDD (3.3 V or 5 V). Make sure that
  matches the SoM's I/O voltage before connecting.
- **Clock:** the MCP2210 shifts MSB first only, at up to 12 MHz. The port clocks
  at **1 MHz** by default (`ONE_MCP2210_BITRATE_HZ`), which is reliable over
  jumper wires. Change it with `--bitrate`. Setting `0` selects about 3 MHz,
  clamped to the range the configuration allows
  (`ABO_SPI_MIN_FREQUENCY_HZ` … `ABO_SPI_FREQUENCY_HZ`).

## Timing limits of a USB bridge

Every MCP2210 transfer needs at least two USB full-speed HID round trips. That
is roughly **2–4 ms per frame** on a quiet PC, with occasional spikes above that
on Windows. The frame length makes almost no difference.

The configuration in `config/` uses a **30 ms cycle** and **10 host-loss
cycles**, so the host-loss timeout is **300 ms**. That suits this bridge:

- A 30 ms cycle leaves plenty of room for the 2–4 ms transfer, so `late` should
  stay at or near zero.
- Only a USB spike over 300 ms makes both ends declare the other lost.

With a shorter cycle, `late` climbs steadily and loss becomes likely. For
bring-up on a PC, keep the **cycle time at 5–10 ms or more** in Anybus Studio.
If the host-loss timeout (cycle × host-loss cycles) is under 20 ms, the build
prints a reminder (`#pragma message`). This setup is a bench and bring-up
tool, not a real-time host.

## Using your own configuration

1. Generate `abo_spi_config.h` in Anybus Studio and load the device from the
   **same** run.
2. Replace `config/abo_spi_config.h` with it. Alternatively, keep yours
   elsewhere and set the CMake cache variable `ONE_CONFIG_DIR` to that folder.
3. Update the accessor names (`ABO_SPI_STATUS_DATA_...`, `ABO_SPI_CONTROL_DATA_...`) in
   `app/main.c`: `FillBeforeFirstCycle()` and `UseTheFrame()`. Report each
   *Input* channel's values as good with `_SetProducerGood`; until then the ONE
   does not use them.

## Files

| Path | What it is |
|---|---|
| `abo-driver-spi/src/` | HMS driver (`abo_spi*.c/h`). Unchanged. |
| `abo-driver-spi/inc/abo_spi.h` | The driver's public header, the only one the application includes. |
| `abo-driver-spi/README.md`, `LICENSE`, `.clang-format` | HMS's driver README, licence and formatting. |
| `config/abo_spi_config.h` | The generated header for the example configuration (layout 0x2811: *Status Data* in, *Control Data* out). Replace it with yours. |
| `port/port.h` | HMS's port header, unchanged: declares `Port_Open()` and `Port_Close()`. |
| `port/port_mcp2210.c/.h` | **The port.** `Port_Open()` with `Transfer` / `NowUs` on the MCP2210, plus configuration, statistics and auto-reconnect. |
| `port/pc_clock.c/.h` | Microsecond clock (QueryPerformanceCounter / CLOCK_MONOTONIC) and sleep. |
| `mcp2210/mcp2210.c/.h` | MCP2210 HID protocol on hidapi: chip and SPI settings, chunked 0x42 transfers, cancel. |
| `app/main.c` | HMS's example main loop with CLI, status line, Ctrl+C. Built as `one_spi`. |
| `.vscode/` | Build/list/run tasks and debug configurations for MSVC and MinGW. |
| `CMakePresets.json` | `msvc`, `mingw` (Windows) and `linux` presets. |

## Troubleshooting

| What you see | Likely cause |
|---|---|
| `open: device could not be opened` | Another program has the MCP2210 open, e.g. MCP2210 Utility. |
| `open: device not found` | Wrong VID/PID (if it was reprogrammed: `--vid/--pid`), or not plugged in. The port retries every second. |
| `idle` climbing, `acc=0` | MISO is not driven: the ONE is unpowered or not started, MISO is unconnected, or CS is on the wrong GP pin. |
| `rep` climbing steadily | The loop runs faster than the ONE produces frames, or the cycle time is too short for it. Not an error now and then. |
| `crc` climbing | Signal integrity. Lower `--bitrate`, shorten wires, check GND, and check that the SPI mode matches. |
| `layout` climbing | The header and the device configuration come from different Studio runs. |
| `late` climbing, frames still accepted | Normal over USB with short cycle times. See [Timing limits](#timing-limits-of-a-usb-bridge). |
| `lost` climbing while frames arrive | USB latency spikes exceed the host-loss timeout. Lengthen the cycle time in Studio. |
| `SPI bus not available` | Another SPI master is driving the bus. |

## Licence

The files in `abo-driver-spi/` and `port/port.h` are © HMS Industrial Networks AB
under the **HMS INT 4-Clause License, Version 1.0**
(`LicenseRef-HMS-INT-4-Clause-1.0`), in `abo-driver-spi/LICENSE`: they may only
be used with HMS products. `config/abo_spi_config.h` is generated by Anybus
Studio. The rest of this project is glue code you may use with them freely. hidapi is fetched at build time under its
own licence (BSD-style / GPLv3 / original HIDAPI licence, at your choice).
