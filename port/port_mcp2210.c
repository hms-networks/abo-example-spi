/*******************************************************************************
** Anybus ONE Fast SPI - porting layer for a PC with a Microchip MCP2210
** USB-to-SPI bridge. Implements examples/ports/port.h,
** in place of HMS's empty examples/ports/template.c.
**
** The two callbacks the driver needs:
**   Transfer - one MCP2210 SPI transaction of exactly `length` bytes: chip
**              select is asserted once for the whole frame.
**   NowUs    - QueryPerformanceCounter (Windows) / CLOCK_MONOTONIC (POSIX).
*******************************************************************************/

#include "port_mcp2210.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "abo_spi.h"
#include "pc_clock.h"

/*******************************************************************************
** Build-time defaults (CMake passes these; see CMakeLists.txt)
*******************************************************************************/

/** @brief GP pin wired to the ONE's CS. */
#ifndef ONE_MCP2210_CS_PIN
#define ONE_MCP2210_CS_PIN 2
#endif

/** @brief SPI clock. 0 = automatic: 3 MHz, clamped into the configuration's range. */
#ifndef ONE_MCP2210_BITRATE_HZ
#define ONE_MCP2210_BITRATE_HZ 1000000
#endif

/** @brief USB serial number of the MCP2210 to use; "" = the first one found. */
#ifndef ONE_MCP2210_SERIAL
#define ONE_MCP2210_SERIAL ""
#endif

/**
 * @brief Floor for the transfer timeout, in ms.
 *
 * The driver asks for one cycle time, which with short cycles no USB round trip
 * can promise. The timeout only guards against a dead USB link, so it is
 * raised to this floor.
 */
#ifndef ONE_MCP2210_MIN_TIMEOUT_MS
#define ONE_MCP2210_MIN_TIMEOUT_MS 50
#endif

/** @brief Clock the automatic setting aims for: robust over short jumper wires. */
#define AUTO_TARGET_BITRATE_HZ 3000000u

/** @brief Gap between attempts to reopen an MCP2210 that went away. */
#define REOPEN_INTERVAL_US 1000000u

/*******************************************************************************
** What the MCP2210 cannot do for a configuration
*******************************************************************************/

#if !ABO_SPI_MSB_FIRST
#error "This configuration is LSB first; the MCP2210 only shifts MSB first."
#endif

#if (ABO_SPI_MODE < 0) || (ABO_SPI_MODE > 3)
#error "ABO_SPI_MODE must be 0..3."
#endif

/* The slowest clock the configuration allows (ABO_SPI_MIN_FREQUENCY_HZ, in preprocessor arithmetic). */
#if ((ABO_SPI_FRAME_LENGTH) * 8000000u / ((ABO_SPI_CYCLE_TIME_US) - (ABO_SPI_MIN_GAP_US))) > 12000000u
#error "This configuration needs an SPI clock above the MCP2210's 12 MHz. Lengthen the cycle time in Anybus Studio."
#endif

/*
** One MCP2210 transfer is at least two USB full-speed HID round trips, about
** 2-4 ms on a quiet PC and occasionally much more on Windows. The device
** declares the host lost after CYCLE_TIME x HOST_LOSS_CYCLES without a frame.
*/
#if ((ABO_SPI_CYCLE_TIME_US) * (ABO_SPI_HOST_LOSS_CYCLES)) < 20000u
#pragma message("Anybus ONE: host-loss timeout under 20 ms. Over an MCP2210 expect ABO_SPI_COUNTER_LATE to climb and occasional device/host loss. For PC testing, use a cycle time of 5-10 ms or more in Anybus Studio.")
#endif

/*******************************************************************************
** Private data
*******************************************************************************/

typedef struct PortState
{
   Mcp2210_t device;
   Mcp2210_Config_t config;
   char serial[64];
   bool configured;      /* Whether Port_Configure ran. */
   bool verbose;
   bool everOpened;
   uint64_t lastOpenAttemptUs;
   int32_t lastError;    /* To print an error once when it changes, not every cycle. */
   Port_Stats_t stats;
} PortState_t;

static PortState_t portState;

/*******************************************************************************
** Private services
*******************************************************************************/

static uint32_t AutomaticBitRate(void)
{
   uint32_t rate = AUTO_TARGET_BITRATE_HZ;

   if (rate > (uint32_t)ABO_SPI_FREQUENCY_HZ)
   {
      rate = (uint32_t)ABO_SPI_FREQUENCY_HZ;
   }

   if (rate > MCP2210_MAX_BITRATE_HZ)
   {
      rate = MCP2210_MAX_BITRATE_HZ;
   }

   if (rate < ABO_SPI_MIN_FREQUENCY_HZ)
   {
      rate = ABO_SPI_MIN_FREQUENCY_HZ;
   }

   return rate;
}

static unsigned long EnvNumber(const char* name, unsigned long fallback)
{
   const char* text = getenv(name);
   char* end = NULL;
   unsigned long value;

   if ((text == NULL) || (text[0] == '\0'))
   {
      return fallback;
   }

   value = strtoul(text, &end, 0);

   return ((end != NULL) && (*end == '\0')) ? value : fallback;
}

/* Layers 1 and 2: build-time defaults, then the environment. */
static void LoadDefaults(void)
{
   const char* serial = getenv("MCP2210_SERIAL");

   memset(&portState.config, 0, sizeof portState.config);

   snprintf(portState.serial, sizeof portState.serial, "%s",
            (serial != NULL) ? serial : ONE_MCP2210_SERIAL);

   portState.config.serial = portState.serial;
   portState.config.vid = (uint16_t)EnvNumber("MCP2210_VID", MCP2210_DEFAULT_VID);
   portState.config.pid = (uint16_t)EnvNumber("MCP2210_PID", MCP2210_DEFAULT_PID);
   portState.config.csPin = (uint8_t)EnvNumber("MCP2210_CS", ONE_MCP2210_CS_PIN);   
   portState.config.bitRateHz = (uint32_t)EnvNumber("MCP2210_BITRATE", ONE_MCP2210_BITRATE_HZ);
   portState.config.spiMode = (uint8_t)ABO_SPI_MODE;
}

static void ReportError(int32_t error, const char* during)
{
   if (portState.verbose || (error != portState.lastError))
   {
      fprintf(stderr, "[mcp2210] %s: %s - %s\n", during, Mcp2210_ResultText(error), portState.device.lastError);
   }

   portState.lastError = error;
}

static bool OpenDevice(void)
{
   Mcp2210_Result_t result;

   portState.lastOpenAttemptUs = PcClock_NowUs();
   result = Mcp2210_Open(&portState.device, &portState.config);

   if (result != MCP2210_OK)
   {
      ReportError(result, "open");
      return false;
   }

   if (portState.everOpened)
   {
      portState.stats.reconnects++;
   }

   portState.everOpened = true;
   portState.lastError = MCP2210_OK;

   fprintf(stderr, "[mcp2210] opened serial \"%s\": CS on GP%u, SPI mode %u, %lu Hz (requested %lu Hz)\n",
           portState.device.serial, (unsigned)portState.config.csPin, (unsigned)portState.config.spiMode,
           (unsigned long)portState.device.actualBitRateHz, (unsigned long)portState.config.bitRateHz);

   if ((portState.device.actualBitRateHz < ABO_SPI_MIN_FREQUENCY_HZ)
       || (portState.device.actualBitRateHz > (uint32_t)ABO_SPI_FREQUENCY_HZ))
   {
      fprintf(stderr, "[mcp2210] warning: %lu Hz is outside the configuration's %lu..%lu Hz\n",
              (unsigned long)portState.device.actualBitRateHz, (unsigned long)ABO_SPI_MIN_FREQUENCY_HZ,
              (unsigned long)ABO_SPI_FREQUENCY_HZ);
   }

   return true;
}

/*------------------------------------------------------------------------------
** Exchange exactly length bytes, full duplex, under one chip-select assertion.
**------------------------------------------------------------------------------
*/
static int32_t Transfer(void* ctx, const uint8_t* tx, uint8_t* rx, uint16_t length, uint32_t timeoutUs)
{
   PortState_t* state = (PortState_t*)ctx;
   uint32_t timeoutMs = (timeoutUs + 999u) / 1000u;
   uint64_t started;
   uint32_t elapsed;
   int32_t result;

   if (!Mcp2210_IsOpen(&state->device))
   {
      /* Unplugged or never found: retry now and then, report a failed transfer meanwhile. */
      if ((PcClock_NowUs() - state->lastOpenAttemptUs) < REOPEN_INTERVAL_US)
      {
         return -1;
      }

      if (!OpenDevice())
      {
         return -1;
      }
   }

   if (timeoutMs < ONE_MCP2210_MIN_TIMEOUT_MS)
   {
      timeoutMs = ONE_MCP2210_MIN_TIMEOUT_MS;
   }

   started = PcClock_NowUs();
   result = Mcp2210_Transfer(&state->device, tx, rx, length, timeoutMs);
   elapsed = (uint32_t)(PcClock_NowUs() - started);

   state->stats.transfers++;

   if (result < 0)
   {
      state->stats.failures++;
      ReportError(result, "transfer");

      if (result == MCP2210_ERR_IO)
      {
         Mcp2210_Close(&state->device); /* Reopened on a later cycle. */
      }

      return -1;
   }

   if (state->lastError != MCP2210_OK)
   {
      fprintf(stderr, "[mcp2210] transfers working again\n");
      state->lastError = MCP2210_OK;
   }

   if ((state->stats.count == 0u) || (elapsed < state->stats.minUs))
   {
      state->stats.minUs = elapsed;
   }

   if (elapsed > state->stats.maxUs)
   {
      state->stats.maxUs = elapsed;
   }

   state->stats.totalUs += elapsed;
   state->stats.count++;

   return result;
}

/*------------------------------------------------------------------------------
** A free-running microsecond counter. Wraps at 32 bits, which the driver expects.
**------------------------------------------------------------------------------
*/
static uint32_t NowUs(void* ctx)
{
   (void)ctx;

   return (uint32_t)PcClock_NowUs();
}

/*******************************************************************************
** Public services
*******************************************************************************/

void Port_Configure(const Port_Options_t* options)
{
   LoadDefaults();
   portState.configured = true;

   if (options == NULL)
   {
      return;
   }

   if (options->serial != NULL)
   {
      snprintf(portState.serial, sizeof portState.serial, "%s", options->serial);
   }

   if (options->csPin >= 0)
   {
      portState.config.csPin = (uint8_t)options->csPin;
   }

   if (options->bitRateHz != 0u)
   {
      portState.config.bitRateHz = options->bitRateHz;
   }

   if (options->vid != 0u)
   {
      portState.config.vid = options->vid;
   }

   if (options->pid != 0u)
   {
      portState.config.pid = options->pid;
   }

   portState.verbose = options->verbose;
}

bool Port_Open(ABO_SPI_Port_t* port)
{
   /* Zeroed first: see ABO_SPI_Port_t. */
   memset(port, 0, sizeof *port);

   if (!portState.configured)
   {
      LoadDefaults();
      portState.configured = true;
   }

   if (portState.config.bitRateHz == 0u)
   {
      portState.config.bitRateHz = AutomaticBitRate();
   }

   PcClock_BeginHighResolution();

   /*
   ** A missing MCP2210 is not a failure: Transfer retries once a second, so the
   ** device can be plugged in later. Port_IsReady tells whether it is open now.
   */
   (void)OpenDevice();

   port->transfer = Transfer;
   port->nowUs = NowUs;
   port->ctx = &portState;

   return true;
}

bool Port_IsReady(void)
{
   return Mcp2210_IsOpen(&portState.device);
}

const Mcp2210_t* Port_Device(void)
{
   return &portState.device;
}

void Port_GetStats(Port_Stats_t* stats, bool resetWindow)
{
   *stats = portState.stats;

   if (resetWindow)
   {
      portState.stats.minUs = 0u;
      portState.stats.maxUs = 0u;
      portState.stats.totalUs = 0u;
      portState.stats.count = 0u;
   }
}

void Port_Close(void)
{
   Mcp2210_Close(&portState.device);
   PcClock_EndHighResolution();
}
