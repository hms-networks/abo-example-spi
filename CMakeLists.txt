cmake_minimum_required(VERSION 3.21) # CMakePresets.json version 3

project(one_spi_mcp2210
        VERSION 0.1.0
        DESCRIPTION "Anybus ONE Fast SPI host driver on a PC through a Microchip MCP2210"
        LANGUAGES C)

# C99 is set per target below (the driver's requirement), not globally:
# hidapi's Linux backend needs the compiler's default GNU dialect.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ------------------------------------------------------------------------------
# Options
# ------------------------------------------------------------------------------

# Where abo_spi_config.h lives. Point this at the header Anybus Studio
# generated for your configuration; the default is the Motor drive
# configuration.
set(ONE_CONFIG_DIR "${CMAKE_CURRENT_SOURCE_DIR}/config" CACHE PATH
    "Directory holding the Anybus Studio generated abo_spi_config.h")

set(ONE_MCP2210_CS_PIN 2 CACHE STRING "MCP2210 GP pin (0..8) wired to the ONE's chip select")
set(ONE_MCP2210_BITRATE_HZ 1000000 CACHE STRING "SPI clock in Hz; 0 = automatic (about 3 MHz)")
set(ONE_MCP2210_SERIAL "" CACHE STRING "USB serial number of the MCP2210 to use; empty = first found")
set(ONE_MCP2210_MIN_TIMEOUT_MS 50 CACHE STRING "Floor for the SPI transfer timeout, in ms")

option(ONE_FETCH_HIDAPI "Download and build hidapi (otherwise find_package(hidapi))" ON)

# ------------------------------------------------------------------------------
# hidapi
# ------------------------------------------------------------------------------

if(ONE_FETCH_HIDAPI)
    include(FetchContent)
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)  # static hidapi: no DLL next to the .exe
    set(HIDAPI_WITH_LIBUSB OFF CACHE BOOL "" FORCE) # Linux: use hidraw
    set(HIDAPI_BUILD_HIDTEST OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(hidapi
        GIT_REPOSITORY https://github.com/libusb/hidapi.git
        GIT_TAG        hidapi-0.15.0
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(hidapi)
    set(ONE_HIDAPI_INCLUDE "${hidapi_SOURCE_DIR}/hidapi")
else()
    find_package(hidapi REQUIRED)
    get_target_property(ONE_HIDAPI_INCLUDE hidapi::hidapi INTERFACE_INCLUDE_DIRECTORIES)
endif()

# ------------------------------------------------------------------------------
# Warnings
# ------------------------------------------------------------------------------

if(MSVC)
    set(ONE_WARNINGS /W4 /wd4204)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
else()
    set(ONE_WARNINGS -Wall -Wextra -pedantic)
endif()

# ------------------------------------------------------------------------------
# The HMS driver (unmodified)
# ------------------------------------------------------------------------------

add_library(abo_spi STATIC
    abo-driver-spi/src/abo_spi.c
    abo-driver-spi/src/abo_spi_bytes.c
    abo-driver-spi/src/abo_spi_frame.c
    abo-driver-spi/src/abo_spi_channel.c)
target_include_directories(abo_spi PUBLIC abo-driver-spi/inc "${ONE_CONFIG_DIR}"
    PRIVATE abo-driver-spi/src)
target_compile_options(abo_spi PRIVATE ${ONE_WARNINGS})

# ------------------------------------------------------------------------------
# The MCP2210 port, in place of HMS's examples/ports/template.c. hidapi is
# linked by the program, not here.
# ------------------------------------------------------------------------------

add_library(one_port_mcp2210 STATIC
    mcp2210/mcp2210.c
    port/port_mcp2210.c
    port/pc_clock.c)
target_include_directories(one_port_mcp2210
    PUBLIC mcp2210 port
    PRIVATE "${ONE_HIDAPI_INCLUDE}")
target_link_libraries(one_port_mcp2210 PUBLIC abo_spi)
target_compile_definitions(one_port_mcp2210 PUBLIC
    ONE_MCP2210_CS_PIN=${ONE_MCP2210_CS_PIN}
    ONE_MCP2210_BITRATE_HZ=${ONE_MCP2210_BITRATE_HZ}u
    ONE_MCP2210_SERIAL="${ONE_MCP2210_SERIAL}"
    ONE_MCP2210_MIN_TIMEOUT_MS=${ONE_MCP2210_MIN_TIMEOUT_MS}u)
target_compile_options(one_port_mcp2210 PRIVATE ${ONE_WARNINGS})
if(WIN32)
    target_link_libraries(one_port_mcp2210 PUBLIC winmm) # timeBeginPeriod
endif()

# ------------------------------------------------------------------------------
# Programs
# ------------------------------------------------------------------------------

# The console application: HMS's example main loop plus status output and CLI.
add_executable(one_spi app/main.c)
target_link_libraries(one_spi PRIVATE one_port_mcp2210 hidapi::hidapi)
target_compile_options(one_spi PRIVATE ${ONE_WARNINGS})

# Convenience targets for VS Code tasks (CMake: Build Target).
add_custom_target(list_mcp2210
    COMMAND one_spi --list
    DEPENDS one_spi
    USES_TERMINAL
    COMMENT "Attached MCP2210 devices")
add_custom_target(run_one_spi
    COMMAND one_spi
    DEPENDS one_spi
    USES_TERMINAL
    COMMENT "Running the console application - Ctrl+C to stop")

set_target_properties(abo_spi one_port_mcp2210 one_spi PROPERTIES
    C_STANDARD 99
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF)
