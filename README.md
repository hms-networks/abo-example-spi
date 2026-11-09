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

The HMS driver is included unchanged as a Git submodule in
`lib/abo-driver-spi/`. HMS's port header (`port/port.h`) is copied unchanged
from the driver's `examples/ports/`. `--dump` reads the received and sent frames from the
handle's public `wireRx` and `wireTx` fields. The port (`Port_Open`, `Port_Close` and the two callbacks
`Transfer` and `NowUs`) is implemented in `port/port_mcp2210.c`.

This README has two parts:

1. [Run the example](#run-the-example) with the example configuration that
   comes with this repository. Start here.
2. [Use your own configuration](#use-your-own-configuration) made in Anybus
   Studio.

## Run the example

The folder `config/` holds a ready-made example configuration in two files
that belong together:

- a **HIFF file**, which you install on the SoM, and
- **`abo_spi_config.h`**, the matching header that the program is built with.

### What you need

| | |
|---|---|
| Anybus ONE Engineering Unit EVK | The evaluation kit with the Anybus ONE SoM. See [About the Anybus ONE evaluation kit (EVK)](https://anybus-one.docs.hms-networks.com/en/about-anybus-one-evaluation-kit--evk-.html). |
| MCP2210 | A Microchip USB-to-SPI bridge, e.g. the [MCP2210 Breakout Module (ADM00419)](https://www.microchip.com/en-us/development-tool/adm00419). |
| The example configuration on the SoM | Install the HIFF file in `config/` on the evaluation board's SoM. See [Install a configuration on the evaluation board SoM](https://anybus-one.docs.hms-networks.com/en/install-a-configuration-on-the-evaluation-board-som.html). |
| VS Code | Extensions: **C/C++** and **CMake Tools**. VS Code offers both when you open the folder. |
| CMake ≥ 3.21 | Ships with Visual Studio. With MSYS2 you can also install it via `pacman`. |
| A C compiler | **Either** Visual Studio 2022 / Build Tools ("Desktop development with C++"), **or** MSYS2 UCRT64: `pacman -S mingw-w64-ucrt-x86_64-{gcc,gdb,ninja,cmake}` with `C:\msys64\ucrt64\bin` on `PATH`. |
| Git + internet on first configure | CMake downloads and builds **hidapi 0.15.0** as a static library. |
| No device driver | The MCP2210 is a USB HID device, so Windows' built-in driver is used. Microchip's DLL is not needed. |

### Wiring

| ONE SoM | MCP2210 | Notes |
|---|---|---|
| SCLK | SCK | |
| MOSI | MOSI (SDO) | |
| MISO | MISO (SDI) | |
| CS | GP2 by default | Any GP0–GP8 pin works; choose it with `--cs` or `ONE_MCP2210_CS_PIN`. The port makes that pin a chip select in the MCP2210's **volatile** settings. NVRAM is never written. |
| GND | GND | Required. |

**Clock:** the MCP2210 shifts MSB first only, at up to 12 MHz. The port clocks
at **1 MHz** by default (`ONE_MCP2210_BITRATE_HZ`), which is reliable over
jumper wires. Change it with `--bitrate`, `MCP2210_BITRATE` or
`ONE_MCP2210_BITRATE_HZ`. In all three, `0` selects about 3 MHz,
clamped to the range the configuration allows
(`ABO_SPI_MIN_FREQUENCY_HZ` … `ABO_SPI_FREQUENCY_HZ`).

### Getting started

The HMS driver is a Git submodule in `lib/abo-driver-spi`, so clone with
`--recurse-submodules`:

```
git clone --recurse-submodules https://github.com/hms-networks/abo-example-spi.git
```

If you already cloned without it, fetch the submodule from inside the repository:

```
git submodule update --init --recursive
```

1. **Open the folder:** in VS Code, choose *File → Open Folder…* and select the
   repository folder. Install the **C/C++** and **CMake Tools** extensions if
   VS Code suggests them.
2. **Select the presets:** VS Code may not ask for a preset, so select them in
   the status bar at the bottom of the window:
   - Click the **configure preset** (it shows *No Configure Preset Selected* at
     first) and pick **MSVC x64 (Visual Studio 2022 / Build Tools)** or
     **MinGW-w64 GCC + Ninja (MSYS2 UCRT64)**, to match your compiler.
   - Click the **build preset** and pick **Debug**.

   You can also do this from the Command Palette (`Ctrl+Shift+P`) with
   *CMake: Select Configure Preset* and *CMake: Select Build Preset*.
3. **Build:** press `Ctrl+Shift+B`. The output appears in the panel at the
   bottom of the window. The first build takes longer, because CMake downloads
   and builds hidapi.
4. **Check that the MCP2210 is found:** plug in the MCP2210 and close
   Microchip's *MCP2210 Utility* if it is open. Then:
   1. Open the **Terminal** menu in the menu bar at the top of the window and
      choose **Run Task…**. (Or press `Ctrl+Shift+P`, type `Tasks: Run Task`
      and press `Enter`.)
   2. Choose **List MCP2210 devices** from the list that opens at the top of
      the window.
   3. The result appears in the **Terminal** panel at the bottom of the window.
      Each connected MCP2210 is listed. If more than one is listed, note the
      serial number of the one you want and pass it with `--serial`: add it to
      `"args"` in `.vscode/launch.json`, or set the `MCP2210_SERIAL`
      environment variable.
5. **Run on hardware:** open the **Run and Debug** view (`Ctrl+Shift+D`), pick
   *Debug (MSVC)* or *Debug (MinGW gdb)* to match your preset in the drop-down
   at the top, and press `F5`. Or, without the debugger, run the task
   *Run one_spi (Ctrl+C to stop)* the same way as in step 4.

#### Program output

The console app prints one status line per second:

```
   12.0s ONLINE    33 fr/s | usb  2.1/ 3.0/ 6.8 ms | acc=395 xfer=0 short=0 layout=0 crc=0 rep=0 idle=0 late=0 lost=0
```

- `fr/s` is the number of frames accepted in the last second.
- `usb` shows the min / average / max time of one MCP2210 transfer.
- The counters after it are the driver's nine counters. Send these with a
  support case.
- With the `--dump` option, it also prints the last frame received on MISO
  (`to-host`) and the last frame sent on MOSI (`from-host`) in hex, header and
  CRC included. Both are the raw wire frames, so `to-host` shows what the ONE
  sent even when the driver rejected it.

#### Turning on `--dump`

`--dump` is a command-line option of the `one_spi` program (see
[Command line](#command-line)). How you pass it depends on how you start the
program:

- **`F5` (debugger):** `--dump` is already on. The debug configurations in
  `.vscode/launch.json` pass it in `"args"`. Remove it there to turn it off.
- **The task *Run one_spi (Ctrl+C to stop)*:** `--dump` is off. This task runs
  `one_spi` without options.
- **From a terminal:** run the built program with the option. In VS Code, open
  a terminal with *Terminal → New Terminal* and run, from the repository
  folder:

  ```
  build\msvc\Debug\one_spi.exe --dump     # MSVC preset
  build\mingw\one_spi.exe --dump          # MinGW preset
  ```

  Stop it with `Ctrl+C`.

### Command line

```
one_spi [--list] [--serial <sn>] [--cs <0..8>] [--bitrate <Hz>]
                [--vid <hex>] [--pid <hex>] [--seconds <n>] [--dump] [--verbose]
```

You can set most options in three ways. If a setting is given in more than one
place, the later one in this list wins:

| Option | 1. CMake cache variable (built in) | 2. Environment variable | 3. Command line |
|---|---|---|---|
| MCP2210 serial number | `ONE_MCP2210_SERIAL` (empty: first found) | `MCP2210_SERIAL` | `--serial` |
| Chip-select pin | `ONE_MCP2210_CS_PIN` (default `2`) | `MCP2210_CS` | `--cs` |
| SPI clock in Hz | `ONE_MCP2210_BITRATE_HZ` (default `1000000`) | `MCP2210_BITRATE` | `--bitrate` |
| USB vendor ID | – | `MCP2210_VID` | `--vid` |
| USB product ID | – | `MCP2210_PID` | `--pid` |

#### Command-line options

Add them after the program name when you run it from a terminal, for example
`build\msvc\Debug\one_spi.exe --cs 3 --dump`. For `F5`, add them to `"args"` in
`.vscode/launch.json`, for example `"args": ["--cs", "3", "--dump"]`.

#### Environment variables

These are read when the program starts, so set them in the terminal you run it
from. In PowerShell (the default VS Code terminal on Windows):

```
$env:MCP2210_CS = "3"
build\msvc\Debug\one_spi.exe
```

The variable stays set until you close that terminal. For `F5`, add it to the
debug configuration in `.vscode/launch.json` instead:

```
"environment": [ { "name": "MCP2210_CS", "value": "3" } ]
```

#### CMake cache variables

These are compiled into the program as its defaults, so you need no options
when you run it. Change them in one of these ways, then build again
(`Ctrl+Shift+B`):

- **In VS Code:** press `Ctrl+Shift+P`, run *CMake: Edit CMake Cache (UI)*,
  change the value and save. Then run *CMake: Configure*.
- **From a terminal:** reconfigure with `-D`, for example
  `cmake --preset msvc -DONE_MCP2210_CS_PIN=3`.

The values stay in the build folder until you delete it or run
*CMake: Delete Cache and Reconfigure*.

### Timing limits of a USB bridge

Every MCP2210 transfer needs at least two USB full-speed HID round trips. That
is roughly **2–4 ms per frame** on a quiet PC, with occasional spikes above that
on Windows. The frame length makes almost no difference.

The example configuration uses a **50 ms cycle** and **8 host-loss cycles**,
so the host-loss timeout is **400 ms**. That suits this bridge:

- A 50 ms cycle leaves plenty of room for the 2–4 ms transfer, so `late` should
  stay at or near zero.
- Only a USB spike over 400 ms makes both ends declare the other lost.

This setup is a bench and bring-up tool, not a real-time host.

## Use your own configuration

Once the example runs, you can replace it with your own configuration. You
create it in **Anybus Studio**, which generates both files you need: a HIFF
file for the SoM and an `abo_spi_config.h` for the program. If you have not
used Anybus Studio before, see
[Create your first configuration in Anybus Studio](https://anybus-one.docs.hms-networks.com/en/create-your-first-configuration-in-anybus-studio.html). Both must come from
the **same** Studio run. If they don't match, the driver rejects the frames
and the `layout` counter climbs.

1. In Anybus Studio, create your configuration and generate the HIFF file and
   `abo_spi_config.h`. Choose the cycle time as described in
   [Choosing the cycle time](#choosing-the-cycle-time).
2. Install the HIFF file on the SoM, the same way as for the example. See
   [Install a configuration on the evaluation board SoM](https://anybus-one.docs.hms-networks.com/en/install-a-configuration-on-the-evaluation-board-som.html).
3. Replace `config/abo_spi_config.h` with your header.
4. Update the accessor names in `app/main.c`, in `FillBeforeFirstCycle()` and
   `UseTheFrame()`. The header names them `ABO_SPI_<DATA SET>_...` after your
   data sets. The example uses `ABO_SPI_STATUSDATASET_...` and
   `ABO_SPI_CONTROLDATASET_...`:
   - For data the host sends, set each value with `..._Set_<VALUE>()`, then
     report them as good with `..._SetProducerGood()`. Until then the ONE does
     not use them.
   - For data the host receives, read each value with `..._Get_<VALUE>()`.
     `..._IsDataValid()` tells you whether the values are live.
5. Build and run as in [Getting started](#getting-started).

### Choosing the cycle time

Because of the [timing limits of a USB bridge](#timing-limits-of-a-usb-bridge),
keep the **cycle time at 5–10 ms or more** for bring-up on a PC. With a shorter
cycle, `late` climbs steadily and loss becomes likely. If the host-loss timeout
(cycle × host-loss cycles) is under 20 ms, the build prints a reminder
(`#pragma message`).

## Reference

### Files

| Path | What it is |
|---|---|
| `lib/abo-driver-spi/` | Git submodule with the HMS driver. Unchanged. |
| `lib/abo-driver-spi/src/` | HMS driver (`abo_spi*.c/h`). |
| `lib/abo-driver-spi/inc/abo_spi.h` | The driver's public header, the only one the application includes. |
| `lib/abo-driver-spi/README.md`, `LICENSE`, `.clang-format` | HMS's driver README, licence and formatting. |
| `config/abo_spi_config.h` | The generated header for the example configuration (layout 0x8BE2: *Status Data Set* sent by the host, *Control Data Set* received by the host). Replace it with yours. |
| `port/port.h` | HMS's port header, unchanged: declares `Port_Open()` and `Port_Close()`. |
| `port/port_mcp2210.c/.h` | **The port.** `Port_Open()` with `Transfer` / `NowUs` on the MCP2210, plus configuration, statistics and auto-reconnect. |
| `port/pc_clock.c/.h` | Microsecond clock (QueryPerformanceCounter / CLOCK_MONOTONIC) and sleep. |
| `mcp2210/mcp2210.c/.h` | MCP2210 HID protocol on hidapi: chip and SPI settings, chunked 0x42 transfers, cancel. |
| `app/main.c` | HMS's example main loop with CLI, status line, Ctrl+C. Built as `one_spi`. |
| `.vscode/` | Build/list/run tasks and debug configurations for MSVC, MinGW and Linux. |
| `CMakeLists.txt` | The build: the driver, the port, `one_spi`, and the `list_mcp2210` / `run_one_spi` targets behind the VS Code tasks. |
| `CMakePresets.json` | `msvc`, `mingw` (Windows) and `linux` presets. |

### Troubleshooting

| What you see | Likely cause |
|---|---|
| `open: device could not be opened` | Another program has the MCP2210 open, e.g. MCP2210 Utility. |
| `open: device not found` | Wrong VID/PID (if it was reprogrammed: `--vid/--pid`), or not plugged in. The port retries every second. |
| `idle` climbing, `acc=0` | MISO is not driven: the ONE is unpowered or not started, MISO is unconnected, or CS is on the wrong GP pin. |
| `rep` climbing steadily | The loop runs faster than the ONE produces frames, or the cycle time is too short for it. Not an error now and then. |
| `crc` climbing | Signal integrity. Lower `--bitrate`, shorten wires, check GND, and check that the SPI mode matches. |
| `layout` climbing | The header the program was built with does not match the configuration on the SoM. With the example, install the HIFF file from `config/` again. With your own configuration, take both files from the same Anybus Studio run. |
| `late` climbing, frames still accepted | Normal over USB with short cycle times. See [Timing limits](#timing-limits-of-a-usb-bridge). |
| `lost` climbing while frames arrive | USB latency spikes exceed the host-loss timeout. Use a longer cycle time, see [Choosing the cycle time](#choosing-the-cycle-time). |
| `SPI bus not available` | Another SPI master is driving the bus. |
