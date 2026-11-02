/*******************************************************************************
** PC clock helpers for the Anybus ONE Fast SPI / MCP2210 port.
**
** A monotonic microsecond clock and a sleep, for Windows and POSIX hosts.
*******************************************************************************/

#ifndef PC_CLOCK_H
#define PC_CLOCK_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Monotonic microseconds since an arbitrary start. Does not wrap in practice. */
uint64_t PcClock_NowUs(void);

/** @brief Sleep for roughly us microseconds. Short waits spin; long waits yield to the OS. */
void PcClock_SleepUs(uint32_t us);

/**
 * @brief Ask the OS for its finest timer resolution (Windows: timeBeginPeriod(1)).
 *        Harmless elsewhere. Pair with PcClock_EndHighResolution.
 */
void PcClock_BeginHighResolution(void);
void PcClock_EndHighResolution(void);

#ifdef __cplusplus
}
#endif

#endif /* PC_CLOCK_H */
