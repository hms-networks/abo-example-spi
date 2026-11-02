/*******************************************************************************
** Anybus ONE Fast SPI - MCP2210 porting layer: the PC-side extras.
**
** port.h (HMS's examples/ports/port.h, unchanged) declares Port_Open() and
** Port_Close(). This header adds what a PC host wants on top: choosing which
** MCP2210 / chip-select pin / bit rate to use, and timing statistics.
**
** Settings are resolved in this order, last wins:
**   1. compile-time defaults (CMake cache: ONE_MCP2210_CS_PIN, ...)
**   2. environment variables MCP2210_SERIAL, MCP2210_CS, MCP2210_BITRATE,
**      MCP2210_VID, MCP2210_PID
**   3. Port_Configure(), called before Port_Open()
*******************************************************************************/

#ifndef PORT_MCP2210_H
#define PORT_MCP2210_H

#include <stdbool.h>
#include <stdint.h>

#include "mcp2210.h"
#include "port.h"

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Overrides for Port_Open. Zero / NULL / -1 fields keep the default. */
typedef struct Port_Options
{
   const char* serial;   /**< MCP2210 USB serial number; NULL keeps the default. */
   int csPin;            /**< GP pin 0..8; -1 keeps the default. */
   uint32_t bitRateHz;   /**< SPI clock; 0 keeps the default (automatic). */
   uint16_t vid;         /**< 0 keeps the default. */
   uint16_t pid;         /**< 0 keeps the default. */
   bool verbose;         /**< Print every transfer error, not only changes. */
} Port_Options_t;

/** @brief Transfer timing, measured around each MCP2210 exchange. */
typedef struct Port_Stats
{
   uint32_t transfers;   /**< Transfers attempted. */
   uint32_t failures;    /**< Transfers that returned an error. */
   uint32_t reconnects;  /**< Times the MCP2210 was reopened after a USB error. */
   uint32_t minUs;       /**< Fastest transfer since the last reset. */
   uint32_t maxUs;       /**< Slowest transfer since the last reset. */
   uint64_t totalUs;     /**< Sum, for the average. */
   uint32_t count;       /**< Transfers timed since the last reset. */
} Port_Stats_t;

/** @brief Set overrides. Call before Port_Open. */
void Port_Configure(const Port_Options_t* options);

/** @brief Whether the MCP2210 is open and programmed. */
bool Port_IsReady(void);

/** @brief The device actually in use, and the SPI clock it reports. */
const Mcp2210_t* Port_Device(void);

/** @brief Copy the statistics, optionally restarting the min/max/average window. */
void Port_GetStats(Port_Stats_t* stats, bool resetWindow);

#ifdef __cplusplus
}
#endif

#endif /* PORT_MCP2210_H */
