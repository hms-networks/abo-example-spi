/*******************************************************************************
** PC clock helpers for the Anybus ONE Fast SPI / MCP2210 port.
*******************************************************************************/

/* Before any system header: clock_gettime and nanosleep under -std=c99. */
#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif

#include "pc_clock.h"

#if defined(_WIN32)

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <mmsystem.h>

uint64_t PcClock_NowUs(void)
{
   static LARGE_INTEGER frequency;
   LARGE_INTEGER counter;

   if (frequency.QuadPart == 0)
   {
      QueryPerformanceFrequency(&frequency);
   }

   QueryPerformanceCounter(&counter);

   /* Split to avoid overflowing counter * 1e6 on long uptimes. */
   return ((uint64_t)(counter.QuadPart / frequency.QuadPart) * 1000000u)
          + ((uint64_t)(counter.QuadPart % frequency.QuadPart) * 1000000u / (uint64_t)frequency.QuadPart);
}

void PcClock_SleepUs(uint32_t us)
{
   uint64_t until = PcClock_NowUs() + us;

   /* Sleep(1) is ~1 ms with timeBeginPeriod(1), and often 15.6 ms without it. */
   if (us > 2000u)
   {
      Sleep((DWORD)((us - 1000u) / 1000u));
   }

   while (PcClock_NowUs() < until)
   {
      SwitchToThread();
   }
}

void PcClock_BeginHighResolution(void)
{
   timeBeginPeriod(1);
}

void PcClock_EndHighResolution(void)
{
   timeEndPeriod(1);
}

#else /* POSIX */

#include <sched.h>
#include <time.h>

uint64_t PcClock_NowUs(void)
{
   struct timespec ts;

   clock_gettime(CLOCK_MONOTONIC, &ts);

   return ((uint64_t)ts.tv_sec * 1000000u) + ((uint64_t)ts.tv_nsec / 1000u);
}

void PcClock_SleepUs(uint32_t us)
{
   uint64_t until = PcClock_NowUs() + us;

   if (us > 200u)
   {
      struct timespec ts;

      ts.tv_sec = (time_t)((us - 100u) / 1000000u);
      ts.tv_nsec = (long)(((us - 100u) % 1000000u) * 1000u);
      nanosleep(&ts, NULL);
   }

   while (PcClock_NowUs() < until)
   {
      sched_yield();
   }
}

void PcClock_BeginHighResolution(void)
{
}

void PcClock_EndHighResolution(void)
{
}

#endif
