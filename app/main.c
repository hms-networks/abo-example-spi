/*******************************************************************************
** Anybus ONE Fast SPI over an MCP2210 - console application for a PC.
**
** The main loop of HMS's examples/io_channels.c, plus what a PC bring-up wants:
** command-line selection of the MCP2210, a once-a-second status line with the
** driver's nine counters and the USB transfer times, and a clean Ctrl+C exit.
**
** UseTheFrame() uses the names of the configuration in config/, exactly
** like HMS's example. When you drop in your own abo_spi_config.h from
** Anybus Studio, change the names in UseTheFrame() and FillBeforeFirstCycle().
*******************************************************************************/

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "abo_spi.h"
#include "pc_clock.h"
#include "port.h"
#include "port_mcp2210.h"

/*******************************************************************************
** Private data
*******************************************************************************/

static ABO_SPI_Handle_t oneLink;

static uint16_t exampleSetpoint;
static uint16_t exampleControlWord;

static bool exampleSetpointIsLive;
static bool wasOnline;
static uint16_t loopbackCounter;

static volatile sig_atomic_t stopRequested;

/*******************************************************************************
** Application - adapt to your generated header
*******************************************************************************/

/* Fill the from-host image before the first cycle (README, Quick start, step 5). */
static void FillBeforeFirstCycle(void)
{
   ABO_SPI_STATUS_DATA_Set_STATUS_WORD(&oneLink, 0u);
   ABO_SPI_STATUS_DATA_Set_ACTUAL_SPEED(&oneLink, 0u);
}

/* A frame was just accepted. Everything read here belongs to it. */
static void UseTheFrame(void)
{
   /* A channel's values are read unconditionally. */
   exampleSetpoint = ABO_SPI_CONTROL_DATA_Get_SPEED_SETPOINT(&oneLink);
   exampleControlWord = ABO_SPI_CONTROL_DATA_Get_CONTROL_WORD(&oneLink);

   /* Live, or the configured substitute? */
   exampleSetpointIsLive = ABO_SPI_CONTROL_DATA_IsDataValid(&oneLink);

   /* A connection ended since the last call: drop what was held for it. */
   if (ABO_SPI_CONTROL_DATA_TakeCloseEvent(&oneLink))
   {
      exampleSetpoint = 0u;
      exampleControlWord = 1u;
   }

   /* Goes out with the next frame. */
   ABO_SPI_STATUS_DATA_Set_STATUS_WORD(&oneLink, exampleControlWord);
   ABO_SPI_STATUS_DATA_Set_ACTUAL_SPEED(&oneLink, exampleSetpoint);

   /* Producer status starts bad at Init; until it is good the ONE ignores these values. */
   ABO_SPI_STATUS_DATA_SetProducerGood(&oneLink, true);
}

/* The device has just stopped answering. Called once per loss. */
static void WhenTheDeviceIsLost(void)
{
   exampleSetpoint = 11u;
   exampleSetpointIsLive = false;
   printf("device lost\n");
}

/*******************************************************************************
** Console plumbing
*******************************************************************************/

static void OnSignal(int signalNumber)
{
   (void)signalNumber;
   stopRequested = 1;
}

static void PrintUsage(const char* program)
{
   printf("Usage: %s [options]\n"
          "  --list               list attached MCP2210s and exit\n"
          "  --serial <text>      use the MCP2210 with this USB serial number\n"
          "  --cs <0..8>          GP pin wired to the ONE's chip select (default %d)\n"
          "  --bitrate <Hz>       SPI clock (default: automatic, ~3 MHz within %lu..%lu Hz)\n"
          "  --vid <hex> --pid <hex>  USB IDs if the MCP2210 was reprogrammed\n"
          "  --seconds <n>        stop after n seconds (default: run until Ctrl+C)\n"
          "  --dump               print the to-host / from-host images each second\n"
          "  --verbose            print every transfer error\n"
          "Environment: MCP2210_SERIAL, MCP2210_CS, MCP2210_BITRATE, MCP2210_VID, MCP2210_PID\n",
          program,
#ifdef ONE_MCP2210_CS_PIN
          ONE_MCP2210_CS_PIN,
#else
          2,
#endif
          (unsigned long)ABO_SPI_MIN_FREQUENCY_HZ, (unsigned long)ABO_SPI_FREQUENCY_HZ);
}

static void PrintHex(const char* label, const uint8_t* bytes, unsigned length)
{
   unsigned i;

   printf("  %-9s", label);

   for (i = 0u; i < length; i++)
   {
      printf(" %02X", bytes[i]);
   }

   printf("\n");
}

static void PrintStatus(double seconds, bool dump)
{
   static uint32_t lastAccepted;
   static const char* const names[ABO_SPI_COUNTER_COUNT] = { "acc",  "xfer", "short", "layout", "crc",
                                                               "rep",  "idle", "late",  "lost" };
   Port_Stats_t stats;
   uint32_t accepted = ABO_SPI_Counter(&oneLink, ABO_SPI_COUNTER_ACCEPTED);
   unsigned i;

   Port_GetStats(&stats, true);

   printf("%7.1fs %-7s %4lu fr/s | usb %4.1f/%4.1f/%4.1f ms |", seconds,
          ABO_SPI_IsOnline(&oneLink) ? "ONLINE" : "offline", (unsigned long)(accepted - lastAccepted),
          stats.count ? stats.minUs / 1000.0 : 0.0, stats.count ? (double)stats.totalUs / stats.count / 1000.0 : 0.0,
          stats.maxUs / 1000.0);

      for (i = 0u; i < ABO_SPI_COUNTER_COUNT; i++)
   {
      printf(" %s=%lu", names[i], (unsigned long)ABO_SPI_Counter(&oneLink, (ABO_SPI_Counter_t)i));
   }

   printf("\n");

   if (dump)
   {
      /* The raw wire frames of the last transfer, MISO and MOSI, accepted or not; the driver has no accessor for them. */
      PrintHex("to-host", oneLink.wireRx, ABO_SPI_FRAME_LENGTH);
      PrintHex("from-host", oneLink.wireTx, ABO_SPI_FRAME_LENGTH);
   }

   fflush(stdout);
   lastAccepted = accepted;
}

/*******************************************************************************
** Public services
*******************************************************************************/

int main(int argc, char** argv)
{
   ABO_SPI_Port_t port;
   Port_Options_t options;
   double runSeconds = 0.0;
   bool dump = false;
   uint64_t startUs;
   uint64_t nextPrintUs;
   int i;
   memset(&options, 0, sizeof options);
   options.csPin = -1;

   for (i = 1; i < argc; i++)
   {
      const char* arg = argv[i];
      const char* value = (i + 1 < argc) ? argv[i + 1] : NULL;

      if (strcmp(arg, "--list") == 0)
      {
         printf("MCP2210 devices:\n");
         if (Mcp2210_PrintDevices(options.vid, options.pid) == 0)
         {
            printf("  none found\n");
         }
         return 0;
      }
      else if ((strcmp(arg, "--serial") == 0) && value)
      {
         options.serial = value;
         i++;
      }
      else if ((strcmp(arg, "--cs") == 0) && value)
      {
         options.csPin = atoi(value);
         i++;
      }
      else if ((strcmp(arg, "--bitrate") == 0) && value)
      {
         options.bitRateHz = (uint32_t)strtoul(value, NULL, 0);
         i++;
      }
      else if ((strcmp(arg, "--vid") == 0) && value)
      {
         options.vid = (uint16_t)strtoul(value, NULL, 16);
         i++;
      }
      else if ((strcmp(arg, "--pid") == 0) && value)
      {
         options.pid = (uint16_t)strtoul(value, NULL, 16);
         i++;
      }
      else if ((strcmp(arg, "--seconds") == 0) && value)
      {
         runSeconds = atof(value);
         i++;
      }
      else if (strcmp(arg, "--dump") == 0)
      {
         dump = true;
      }
      else if (strcmp(arg, "--verbose") == 0)
      {
         options.verbose = true;
      }
      else
      {
         PrintUsage(argv[0]);
         return (strcmp(arg, "--help") == 0 || strcmp(arg, "-h") == 0) ? 0 : 2;
      }
   }

   printf("Anybus ONE Fast SPI driver %s over MCP2210\n", ABO_SPI_DRIVER_VERSION);
#if defined(ABO_SPI_CONFIG_NAME) && defined(ABO_SPI_CONFIG_HASH)
   /* Not every generated header carries the name and hash. */
   printf("Configuration \"%s\" (%s), ", ABO_SPI_CONFIG_NAME, ABO_SPI_CONFIG_HASH);
#endif
   printf("layout 0x%04X: frame %u bytes, cycle %u us, host loss %u cycles, SPI mode %d\n",
          (unsigned)ABO_SPI_LAYOUT_ID, (unsigned)ABO_SPI_FRAME_LENGTH, (unsigned)ABO_SPI_CYCLE_TIME_US,
          (unsigned)ABO_SPI_HOST_LOSS_CYCLES, ABO_SPI_MODE);

   signal(SIGINT, OnSignal);
   signal(SIGTERM, OnSignal);

   Port_Configure(&options);

   if (!Port_Open(&port) || (ABO_SPI_Init(&oneLink, &port) != ABO_SPI_OK))
   {
      fprintf(stderr, "Port_Open or ABO_SPI_Init failed\n");
      Port_Close();
      return 1;
   }

   if (!Port_IsReady())
   {
      printf("Waiting for the MCP2210 (retrying every second; Ctrl+C to quit)...\n");
   }

   FillBeforeFirstCycle();

   startUs = PcClock_NowUs();
   nextPrintUs = startUs + 1000000u;

   while (!stopRequested)
   {
      ABO_SPI_Result_t result;
      bool online;
      uint32_t wait = ABO_SPI_UsUntilDue(&oneLink);
      uint64_t now;

      /* Sleep rather than spin when there is time (README, "An RTOS task or a thread"). */
      if (wait > 0u)
      {
         PcClock_SleepUs(wait);
      }

      result = ABO_SPI_RunCycle(&oneLink);
      online = ABO_SPI_IsOnline(&oneLink);

      /* The ONE stopped answering: act once, on the change (README, "Outside the loop"). */
      if (wasOnline && !online)
      {
         WhenTheDeviceIsLost();
      }

      wasOnline = online;

      if (result == ABO_SPI_OK)
      {
         UseTheFrame();
      }
      else if ((result != ABO_SPI_NOT_DUE) && options.verbose)
      {
         printf("RunCycle: result %d\n", (int)result);
      }

      now = PcClock_NowUs();

      if (now >= nextPrintUs)
      {
         PrintStatus((double)(now - startUs) / 1e6, dump);
         nextPrintUs += 1000000u;

         if ((runSeconds > 0.0) && ((double)(now - startUs) / 1e6 >= runSeconds))
         {
            break;
         }
      }
   }

   printf("Driver %s counters:", ABO_SPI_DRIVER_VERSION);

   for (i = 0; i < (int)ABO_SPI_COUNTER_COUNT; i++)
   {
      printf(" %lu", (unsigned long)ABO_SPI_Counter(&oneLink, (ABO_SPI_Counter_t)i));
   }

   printf("\n");

   Port_Close();

   return 0;
}
