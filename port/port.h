/*
 * SPDX-License-Identifier: LicenseRef-HMS-INT-4-Clause-1.0
 * Copyright (c) 2026 HMS Industrial Networks AB
 *
 * This file is part of the HMS Anybus ONE SDK and must only be used with
 * HMS products. See the LICENSE file in the root of the SDK for full
 * license terms.
 *
 * File:        port.h
 * Description: The contract every port implements
 */

#ifndef ABO_SPI_EXAMPLE_PORT_H
#define ABO_SPI_EXAMPLE_PORT_H

#include "abo_spi.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Fill in the two callbacks the driver needs, and open whatever they
 *        talk to.
 *
 * This is the whole platform surface. A port implements these two functions
 * and nothing else, which is the point: moving the driver to a new processor
 * or operating system is one file of about a hundred lines.
 *
 * Call it before `ABO_SPI_Init`, and pass the same port to it.
 *
 * @param port Zeroed and filled with `transfer`, `nowUs` and `ctx`.
 * @return false when the hardware behind it could not be opened. The reason is
 *         printed by the port, which is the only place that knows what it was.
 */
bool Port_Open(ABO_SPI_Port_t* port);

/** @brief Release whatever `Port_Open` took. Safe to call after a failed open. */
void Port_Close(void);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* ABO_SPI_EXAMPLE_PORT_H */
