/*******************************************************************************
** MCP2210 USB-to-SPI bridge - minimal host driver over hidapi.
**
** Implements the subset of the MCP2210 HID command set (Microchip DS22288)
** that an SPI master needs:
**
**   0x10 Get status            0x11 Cancel SPI transfer
**   0x20 Get chip settings     0x21 Set chip settings     (volatile, RAM only)
**   0x40 Set SPI settings      0x41 Get SPI settings      (volatile, RAM only)
**   0x42 Transfer SPI data
**
** Nothing is written to the MCP2210's NVRAM: power-up defaults are untouched.
*******************************************************************************/

#ifndef MCP2210_H
#define MCP2210_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Microchip's default USB IDs for the MCP2210. */
#define MCP2210_DEFAULT_VID 0x04D8u
#define MCP2210_DEFAULT_PID 0x00DEu

/** @brief The MCP2210's SPI clock limits. */
#define MCP2210_MAX_BITRATE_HZ 12000000u
#define MCP2210_MIN_BITRATE_HZ 1500u

/** @brief GP pins 0..8 can be chip selects. */
#define MCP2210_GP_PIN_COUNT 9u

/** @brief Result codes. Every failure is negative. */
typedef enum Mcp2210_Result
{
   MCP2210_OK = 0,
   MCP2210_ERR_HID_INIT = -1,      /**< hid_init() failed. */
   MCP2210_ERR_NOT_FOUND = -2,     /**< No MCP2210 with that VID/PID/serial. */
   MCP2210_ERR_OPEN = -3,          /**< Found, but hid_open failed (in use by another program?). */
   MCP2210_ERR_IO = -4,            /**< hid_write/hid_read failed: unplugged? */
   MCP2210_ERR_TIMEOUT = -5,       /**< No response, or the transfer did not finish in time. */
   MCP2210_ERR_PROTOCOL = -6,      /**< The MCP2210 answered something unexpected. */
   MCP2210_ERR_BUS_NOT_AVAILABLE = -7, /**< 0xF7: another master owns the SPI bus. */
   MCP2210_ERR_ARGUMENT = -8,      /**< Bad parameter. */
   MCP2210_ERR_NOT_OPEN = -9       /**< The handle has no open device. */
} Mcp2210_Result_t;

/** @brief How to find and set up the MCP2210. */
typedef struct Mcp2210_Config
{
   uint16_t vid;        /**< USB vendor ID; 0 = MCP2210_DEFAULT_VID. */
   uint16_t pid;        /**< USB product ID; 0 = MCP2210_DEFAULT_PID. */
   const char* serial;  /**< USB serial number (ASCII); NULL or "" = first device found. */
   uint8_t csPin;       /**< GP pin (0..8) used as chip select for the ONE. */
   uint32_t bitRateHz;  /**< Requested SPI clock. */
   uint8_t spiMode;     /**< SPI mode 0..3. */
} Mcp2210_Config_t;

/** @brief One open MCP2210. Treat the fields as private. */
typedef struct Mcp2210
{
   void* hid;                   /**< hid_device*, opaque here so callers need not include hidapi.h. */
   Mcp2210_Config_t config;     /**< As passed to Open, serial pointer not owned. */
   uint16_t transferSize;       /**< Bytes per SPI transaction currently programmed. */
   uint32_t actualBitRateHz;    /**< What the MCP2210 reports back after Set SPI settings. */
   char serial[64];             /**< Serial number of the device actually opened. */
   char lastError[160];         /**< Human-readable detail of the last failure. */
} Mcp2210_t;

/** @brief Print every attached MCP2210 (VID/PID, serial, product, path) to stdout. Returns the count. */
int Mcp2210_PrintDevices(uint16_t vid, uint16_t pid);

/**
 * @brief Open the device and program, in volatile memory only:
 *        - the chosen GP pin as a chip select, idle high, active low
 *        - SPI mode, bit rate, zero delays
 *        Any SPI transfer left running by a previous program is cancelled first.
 */
Mcp2210_Result_t Mcp2210_Open(Mcp2210_t* dev, const Mcp2210_Config_t* config);

/** @brief Cancel any transfer in progress and close. Safe on a closed handle. */
void Mcp2210_Close(Mcp2210_t* dev);

/** @brief Whether the handle holds an open device. */
bool Mcp2210_IsOpen(const Mcp2210_t* dev);

/**
 * @brief Exchange length bytes full duplex, with chip select asserted once for the whole transfer.
 *
 * Reprograms the transaction size first if length differs from the last call.
 *
 * @return length on success, the number of bytes received if the MCP2210 ended
 *         the transaction early, or a negative Mcp2210_Result_t.
 */
int32_t Mcp2210_Transfer(Mcp2210_t* dev, const uint8_t* tx, uint8_t* rx, uint16_t length, uint32_t timeoutMs);

/** @brief Text for a result code. */
const char* Mcp2210_ResultText(int32_t result);

#ifdef __cplusplus
}
#endif

#endif /* MCP2210_H */
