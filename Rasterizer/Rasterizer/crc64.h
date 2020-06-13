/*
 * *****************************************************************************
 *                   Copyright (C) 2014, UChicago Argonne, LLC
 *                              All Rights Reserved
 *            High-Performance CRC64 Library (ANL-SF-14-095)
 *                    Hal Finkel, Argonne National Laboratory
 *
 *                              OPEN SOURCE LICENSE
 *
 * Under the terms of Contract No. DE-AC02-06CH11357 with UChicago Argonne, LLC,
 * the U.S. Government retains certain rights in this software.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * 3. Neither the names of UChicago Argonne, LLC or the Department of Energy nor
 *    the names of its contributors may be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * *****************************************************************************
 *                                  DISCLAIMER
 *
 * THE SOFTWARE IS SUPPLIED "AS IS" WITHOUT WARRANTY OF ANY KIND.
 *
 * NEITHER THE UNTED STATES GOVERNMENT, NOR THE UNITED STATES DEPARTMENT OF
 * ENERGY, NOR UCHICAGO ARGONNE, LLC, NOR ANY OF THEIR EMPLOYEES, MAKES ANY
 * WARRANTY, EXPRESS OR IMPLIED, OR ASSUMES ANY LEGAL LIABILITY OR RESPONSIBILITY
 * FOR THE ACCURACY, COMPLETENESS, OR USEFULNESS OF ANY INFORMATION, DATA,
 * APPARATUS, PRODUCT, OR PROCESS DISCLOSED, OR REPRESENTS THAT ITS USE WOULD NOT
 * INFRINGE PRIVATELY OWNED RIGHTS.
 *
 * *****************************************************************************
 */

#include <stdlib.h>
#include <stdint.h>

/*
 * These functions compute the CRC-64 checksum on a block of data
 * and provide a way to combine the checksums on two blocks of data.
 * For more information, see:
 * http://en.wikipedia.org/wiki/Computation_of_CRC
 * http://checksumcrc.blogspot.com/2011/12/should-you-use-crc-or-checksum.html
 * http://crcutil.googlecode.com/files/crc-doc.1.0.pdf
 * http://www.ross.net/crc/download/crc_v3.txt
 * This implementation uses the ECMA-182 polynomial with -1 initialization, and
 * computes the bit-reversed CRC.
 */

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Calculate the CRC64 of the provided buffer using the slow reference
 * implementation (in serial).
 */
extern uint64_t crc64_slow(size_t crc, const void *input, size_t nbytes);

/*
 * Calculate the CRC64 of the provided buffer (in serial).
 */
extern uint64_t crc64(uint64_t crc, const void *input, size_t nbytes);

/*
 * Calculate the CRC64 of the provided buffer, in parallel if possible.
 */
uint64_t crc64_omp(const void *input, size_t nbytes);

/*
 * Calculate the 'check bytes' for the provided CRC64. If these bytes are
 * appended to the original buffer, then the new total CRC64 should be -1.
 */
void crc64_invert(uint64_t cs, void *check_bytes);

/*
 * Given the CRC64 of the first part of a buffer, and the CRC64 and length of
 * the second part of a buffer, calculate the CRC64 of the complete buffer.
 */
uint64_t crc64_combine(uint64_t cs1, uint64_t cs2, size_t nbytes2);

#ifdef __cplusplus
}
#endif

