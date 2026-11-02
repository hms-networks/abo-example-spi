/*******************************************************************************
** MCP2210 USB-to-SPI bridge - minimal host driver over hidapi.
**
** Every command is one 64-byte HID output report answered by one 64-byte
** input report. Byte 0 is the command code in both; byte 1 of the answer is
** the status. Multi-byte fields are little-endian. See Microchip DS22288,
** section 3 "USB HID commands".
*******************************************************************************/

#include "mcp2210.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>

#include <hidapi.h>

#include "pc_clock.h"

/*******************************************************************************
** Protocol constants
*******************************************************************************/

#define REPORT_SIZE 64u

#define CMD_GET_STATUS 0x10u
#define CMD_CANCEL_TRANSFER 0x11u
#define CMD_GET_CHIP_SETTINGS 0x20u
#define CMD_SET_CHIP_SETTINGS 0x21u
#define CMD_SET_SPI_SETTINGS 0x40u
#define CMD_GET_SPI_SETTINGS 0x41u
#define CMD_TRANSFER_SPI 0x42u

#define STATUS_OK 0x00u
#define STATUS_BUS_NOT_AVAILABLE 0xF7u
#define STATUS_TRANSFER_IN_PROGRESS 0xF8u

/* Byte 3 of a 0x42 answer: the SPI engine's state. */
#define ENGINE_FINISHED 0x10u
#define ENGINE_STARTED_NO_DATA 0x20u
#define ENGINE_DATA_AVAILABLE 0x30u

/* Most SPI bytes one 0x42 report carries, either way. */
#define TRANSFER_CHUNK 60u

/* GP pin designation codes in chip settings. */
#define GP_DESIGNATION_GPIO 0x00u
#define GP_DESIGNATION_CHIP_SELECT 0x01u

/* How long a single command waits for its answer. */
#define COMMAND_TIMEOUT_MS 200

/*******************************************************************************
** Private services
*******************************************************************************/

static void SetError(Mcp2210_t* dev, const char* format, ...)
{
   va_list args;

   va_start(args, format);
   vsnprintf(dev->lastError, sizeof dev->lastError, format, args);
   va_end(args);
}

static void WrU16(uint8_t* at, uint16_t value)
{
   at[0] = (uint8_t)(value & 0xFFu);
   at[1] = (uint8_t)(value >> 8);
}

static void WrU32(uint8_t* at, uint32_t value)
{
   at[0] = (uint8_t)(value & 0xFFu);
   at[1] = (uint8_t)((value >> 8) & 0xFFu);
   at[2] = (uint8_t)((value >> 16) & 0xFFu);
   at[3] = (uint8_t)(value >> 24);
}

static uint16_t RdU16(const uint8_t* at)
{
   return (uint16_t)(at[0] | (at[1] << 8));
}

static uint32_t RdU32(const uint8_t* at)
{
   return (uint32_t)at[0] | ((uint32_t)at[1] << 8) | ((uint32_t)at[2] << 16) | ((uint32_t)at[3] << 24);
}

/*------------------------------------------------------------------------------
** Send one 64-byte command and wait for its 64-byte answer.
**------------------------------------------------------------------------------
*/
static Mcp2210_Result_t Command(Mcp2210_t* dev, const uint8_t request[REPORT_SIZE], uint8_t response[REPORT_SIZE],
                                int timeoutMs)
{
   /* hidapi wants the report ID first; the MCP2210 uses none, so it is 0. */
   uint8_t out[REPORT_SIZE + 1u];
   int got;
   uint64_t deadline;

   if (dev->hid == NULL)
   {
      return MCP2210_ERR_NOT_OPEN;
   }

   out[0] = 0x00u;
   memcpy(&out[1], request, REPORT_SIZE);

   if (hid_write((hid_device*)dev->hid, out, sizeof out) < 0)
   {
      SetError(dev, "hid_write failed: %ls", hid_error((hid_device*)dev->hid));
      return MCP2210_ERR_IO;
   }

   deadline = PcClock_NowUs() + ((uint64_t)timeoutMs * 1000u);

   /* Skip any stale answer to an earlier command (e.g. after a timeout). */
   for (;;)
   {
      uint64_t now = PcClock_NowUs();
      int remainingMs = (now >= deadline) ? 0 : (int)((deadline - now + 999u) / 1000u);

      got = hid_read_timeout((hid_device*)dev->hid, response, REPORT_SIZE, remainingMs);

      if (got < 0)
      {
         SetError(dev, "hid_read failed: %ls", hid_error((hid_device*)dev->hid));
         return MCP2210_ERR_IO;
      }

      if (got == 0)
      {
         SetError(dev, "no answer to command 0x%02X within %d ms", request[0], timeoutMs);
         return MCP2210_ERR_TIMEOUT;
      }

      if (response[0] == request[0])
      {
         return MCP2210_OK;
      }
   }
}

static Mcp2210_Result_t SimpleCommand(Mcp2210_t* dev, uint8_t code, uint8_t response[REPORT_SIZE])
{
   uint8_t request[REPORT_SIZE];

   memset(request, 0, sizeof request);
   request[0] = code;

   return Command(dev, request, response, COMMAND_TIMEOUT_MS);
}

/*------------------------------------------------------------------------------
** Make csPin a chip select, leaving every other pin as the device had it.
**------------------------------------------------------------------------------
*/
static Mcp2210_Result_t ConfigureChipSelect(Mcp2210_t* dev)
{
   uint8_t request[REPORT_SIZE];
   uint8_t response[REPORT_SIZE];
   Mcp2210_Result_t result = SimpleCommand(dev, CMD_GET_CHIP_SETTINGS, response);

   if (result != MCP2210_OK)
   {
      return result;
   }

   if (response[1] != STATUS_OK)
   {
      SetError(dev, "Get chip settings answered status 0x%02X", response[1]);
      return MCP2210_ERR_PROTOCOL;
   }

   if (response[4u + dev->config.csPin] == GP_DESIGNATION_CHIP_SELECT)
   {
      return MCP2210_OK; /* Already a chip select. */
   }

   /*
   ** Answer bytes 4..17 are, in order, the GP0..GP8 designations, the default
   ** GPIO output and direction words and the "other settings" byte - the same
   ** layout Set chip settings takes. Bytes 18.. (NVRAM access control,
   ** password) are left zero: they apply to NVRAM writes, which this is not.
   */
   memset(request, 0, sizeof request);
   request[0] = CMD_SET_CHIP_SETTINGS;
   memcpy(&request[4], &response[4], 14u);
   request[4u + dev->config.csPin] = GP_DESIGNATION_CHIP_SELECT;

   result = Command(dev, request, response, COMMAND_TIMEOUT_MS);

   if (result != MCP2210_OK)
   {
      return result;
   }

   if (response[1] != STATUS_OK)
   {
      SetError(dev, "Set chip settings answered status 0x%02X", response[1]);
      return MCP2210_ERR_PROTOCOL;
   }

   return MCP2210_OK;
}

/*------------------------------------------------------------------------------
** Program bit rate, chip-select levels, delays, transaction size and mode.
**------------------------------------------------------------------------------
*/
static Mcp2210_Result_t ConfigureSpi(Mcp2210_t* dev, uint16_t transferSize)
{
   uint8_t request[REPORT_SIZE];
   uint8_t response[REPORT_SIZE];
   uint16_t csMask = (uint16_t)(1u << dev->config.csPin);
   uint16_t idle = 0x01FFu;                   /* Every chip select high while idle. */
   uint16_t active = (uint16_t)(idle & ~csMask); /* Only ours low while active. */
   Mcp2210_Result_t result;
   int attempt;

   memset(request, 0, sizeof request);
   request[0] = CMD_SET_SPI_SETTINGS;
   WrU32(&request[4], dev->config.bitRateHz);
   WrU16(&request[8], idle);
   WrU16(&request[10], active);
   WrU16(&request[12], 0u); /* CS to first data byte, 100 us units. */
   WrU16(&request[14], 0u); /* Last data byte to CS release. */
   WrU16(&request[16], 0u); /* Between data bytes. */
   WrU16(&request[18], transferSize);
   request[20] = dev->config.spiMode;

   /* The MCP2210 refuses new settings while a transfer is still running. */
   for (attempt = 0; attempt < 20; attempt++)
   {
      result = Command(dev, request, response, COMMAND_TIMEOUT_MS);

      if (result != MCP2210_OK)
      {
         return result;
      }

      if (response[1] != STATUS_TRANSFER_IN_PROGRESS)
      {
         break;
      }

      PcClock_SleepUs(1000u);
   }

   if (response[1] != STATUS_OK)
   {
      SetError(dev, "Set SPI settings answered status 0x%02X", response[1]);
      return MCP2210_ERR_PROTOCOL;
   }

   /* Read back what the MCP2210 actually took. */
   result = SimpleCommand(dev, CMD_GET_SPI_SETTINGS, response);

   if (result != MCP2210_OK)
   {
      return result;
   }

   if ((response[1] != STATUS_OK) || (RdU16(&response[18]) != transferSize) || (response[20] != dev->config.spiMode)
       || (RdU16(&response[10]) != active))
   {
      SetError(dev, "SPI settings did not stick (status 0x%02X, size %u, mode %u)", response[1],
               (unsigned)RdU16(&response[18]), (unsigned)response[20]);
      return MCP2210_ERR_PROTOCOL;
   }

   dev->actualBitRateHz = RdU32(&response[4]);
   dev->transferSize = transferSize;

   return MCP2210_OK;
}

static void CancelTransfer(Mcp2210_t* dev)
{
   uint8_t response[REPORT_SIZE];

   (void)SimpleCommand(dev, CMD_CANCEL_TRANSFER, response);
}

/*******************************************************************************
** Public services
*******************************************************************************/

int Mcp2210_PrintDevices(uint16_t vid, uint16_t pid)
{
   struct hid_device_info* list;
   struct hid_device_info* it;
   int count = 0;

   if (hid_init() != 0)
   {
      printf("hid_init failed\n");
      return 0;
   }

   list = hid_enumerate(vid ? vid : MCP2210_DEFAULT_VID, pid ? pid : MCP2210_DEFAULT_PID);

   for (it = list; it != NULL; it = it->next)
   {
      count++;
      printf("  [%d] %04X:%04X  serial \"%ls\"  %ls %ls\n       %s\n", count, it->vendor_id, it->product_id,
             it->serial_number ? it->serial_number : L"", it->manufacturer_string ? it->manufacturer_string : L"",
             it->product_string ? it->product_string : L"", it->path);
   }

   hid_free_enumeration(list);

   return count;
}

Mcp2210_Result_t Mcp2210_Open(Mcp2210_t* dev, const Mcp2210_Config_t* config)
{
   struct hid_device_info* list;
   struct hid_device_info* it;
   struct hid_device_info* chosen = NULL;
   Mcp2210_Result_t result;
   uint8_t response[REPORT_SIZE];

   if ((dev == NULL) || (config == NULL) || (config->csPin >= MCP2210_GP_PIN_COUNT) || (config->spiMode > 3u)
       || (config->bitRateHz < MCP2210_MIN_BITRATE_HZ) || (config->bitRateHz > MCP2210_MAX_BITRATE_HZ))
   {
      if (dev != NULL)
      {
         SetError(dev, "invalid configuration (cs 0..8, mode 0..3, bit rate %u..%u Hz)",
                  (unsigned)MCP2210_MIN_BITRATE_HZ, (unsigned)MCP2210_MAX_BITRATE_HZ);
      }

      return MCP2210_ERR_ARGUMENT;
   }

   memset(dev, 0, sizeof *dev);
   dev->config = *config;
   dev->config.vid = config->vid ? config->vid : MCP2210_DEFAULT_VID;
   dev->config.pid = config->pid ? config->pid : MCP2210_DEFAULT_PID;

   if (hid_init() != 0)
   {
      SetError(dev, "hid_init failed");
      return MCP2210_ERR_HID_INIT;
   }

   /* Open by path, so that the serial match is a plain ASCII compare we control. */
   list = hid_enumerate(dev->config.vid, dev->config.pid);

   for (it = list; it != NULL; it = it->next)
   {
      char serial[sizeof dev->serial] = "";

      if (it->serial_number != NULL)
      {
         snprintf(serial, sizeof serial, "%ls", it->serial_number);
      }

      if ((config->serial == NULL) || (config->serial[0] == '\0') || (strcmp(serial, config->serial) == 0))
      {
         chosen = it;
         memcpy(dev->serial, serial, sizeof dev->serial);
         break;
      }
   }

   if (chosen == NULL)
   {
      hid_free_enumeration(list);
      SetError(dev, "no MCP2210 %04X:%04X%s%s attached", dev->config.vid, dev->config.pid,
               (config->serial && config->serial[0]) ? " with serial " : "",
               (config->serial && config->serial[0]) ? config->serial : "");
      return MCP2210_ERR_NOT_FOUND;
   }

   dev->hid = hid_open_path(chosen->path);
   hid_free_enumeration(list);

   if (dev->hid == NULL)
   {
      SetError(dev, "hid_open_path failed - is another program (e.g. MCP2210 Utility) holding the device?");
      return MCP2210_ERR_OPEN;
   }

   /* Blocking reads are done with explicit timeouts only. */
   hid_set_nonblocking((hid_device*)dev->hid, 0);

   /* A transfer an earlier program left half done would swallow our first frames. */
   CancelTransfer(dev);

   result = SimpleCommand(dev, CMD_GET_STATUS, response);

   if (result == MCP2210_OK)
   {
      result = ConfigureChipSelect(dev);
   }

   if (result == MCP2210_OK)
   {
      result = ConfigureSpi(dev, 1u);
   }

   if (result != MCP2210_OK)
   {
      hid_close((hid_device*)dev->hid);
      dev->hid = NULL;
   }

   return result;
}

void Mcp2210_Close(Mcp2210_t* dev)
{
   if ((dev != NULL) && (dev->hid != NULL))
   {
      CancelTransfer(dev);
      hid_close((hid_device*)dev->hid);
      dev->hid = NULL;
   }
}

bool Mcp2210_IsOpen(const Mcp2210_t* dev)
{
   return (dev != NULL) && (dev->hid != NULL);
}

int32_t Mcp2210_Transfer(Mcp2210_t* dev, const uint8_t* tx, uint8_t* rx, uint16_t length, uint32_t timeoutMs)
{
   uint8_t request[REPORT_SIZE];
   uint8_t response[REPORT_SIZE];
   uint16_t sent = 0u;
   uint16_t received = 0u;
   uint64_t deadline;
   Mcp2210_Result_t result;

   if (!Mcp2210_IsOpen(dev))
   {
      return MCP2210_ERR_NOT_OPEN;
   }

   if ((tx == NULL) || (rx == NULL) || (length == 0u))
   {
      return MCP2210_ERR_ARGUMENT;
   }

   if (length != dev->transferSize)
   {
      result = ConfigureSpi(dev, length);

      if (result != MCP2210_OK)
      {
         return result;
      }
   }

   deadline = PcClock_NowUs() + ((uint64_t)timeoutMs * 1000u);

   /*
   ** Feed up to 60 bytes per report. The MCP2210 asserts chip select on the
   ** first byte of the transaction and releases it after `length` bytes, so
   ** splitting into reports does not split the frame. Once everything is sent,
   ** empty reports collect what is still to come back.
   */
   for (;;)
   {
      uint16_t chunk = (uint16_t)(length - sent);
      int remainingMs;
      uint64_t now = PcClock_NowUs();

      if (now >= deadline)
      {
         CancelTransfer(dev);
         SetError(dev, "SPI transfer of %u bytes not finished within %u ms (%u sent, %u received)",
                  (unsigned)length, (unsigned)timeoutMs, (unsigned)sent, (unsigned)received);
         return MCP2210_ERR_TIMEOUT;
      }

      remainingMs = (int)((deadline - now + 999u) / 1000u);

      if (chunk > TRANSFER_CHUNK)
      {
         chunk = TRANSFER_CHUNK;
      }

      memset(request, 0, sizeof request);
      request[0] = CMD_TRANSFER_SPI;
      request[1] = (uint8_t)chunk;
      memcpy(&request[4], &tx[sent], chunk);

      result = Command(dev, request, response, (remainingMs < COMMAND_TIMEOUT_MS) ? COMMAND_TIMEOUT_MS : remainingMs);

      if (result != MCP2210_OK)
      {
         return result;
      }

      if (response[1] == STATUS_TRANSFER_IN_PROGRESS)
      {
         continue; /* Chunk not taken; offer it again. */
      }

      if (response[1] == STATUS_BUS_NOT_AVAILABLE)
      {
         SetError(dev, "SPI bus not available (another master is driving it)");
         return MCP2210_ERR_BUS_NOT_AVAILABLE;
      }

      if (response[1] != STATUS_OK)
      {
         SetError(dev, "Transfer SPI data answered status 0x%02X", response[1]);
         CancelTransfer(dev);
         return MCP2210_ERR_PROTOCOL;
      }

      sent = (uint16_t)(sent + chunk);

      if ((response[2] > TRANSFER_CHUNK) || ((uint32_t)received + response[2] > length))
      {
         SetError(dev, "MCP2210 returned %u bytes, more than asked for", (unsigned)(received + response[2]));
         CancelTransfer(dev);
         return MCP2210_ERR_PROTOCOL;
      }

      memcpy(&rx[received], &response[4], response[2]);
      received = (uint16_t)(received + response[2]);

      if (response[3] == ENGINE_FINISHED)
      {
         return (int32_t)received;
      }

      if ((response[3] != ENGINE_STARTED_NO_DATA) && (response[3] != ENGINE_DATA_AVAILABLE))
      {
         SetError(dev, "unknown SPI engine state 0x%02X", response[3]);
         CancelTransfer(dev);
         return MCP2210_ERR_PROTOCOL;
      }
   }
}

const char* Mcp2210_ResultText(int32_t result)
{
   switch (result)
   {
   case MCP2210_OK: return "OK";
   case MCP2210_ERR_HID_INIT: return "HID init failed";
   case MCP2210_ERR_NOT_FOUND: return "device not found";
   case MCP2210_ERR_OPEN: return "device could not be opened";
   case MCP2210_ERR_IO: return "USB I/O error";
   case MCP2210_ERR_TIMEOUT: return "timeout";
   case MCP2210_ERR_PROTOCOL: return "unexpected answer";
   case MCP2210_ERR_BUS_NOT_AVAILABLE: return "SPI bus not available";
   case MCP2210_ERR_ARGUMENT: return "bad argument";
   case MCP2210_ERR_NOT_OPEN: return "device not open";
   default: return (result >= 0) ? "OK" : "unknown error";
   }
}
