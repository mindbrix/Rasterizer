//
//  crc64.h
//  Rasterizer
//
//  Created by Nigel Barber on 28/02/2019.
//  Copyright © 2019 @mindbrix. All rights reserved.
//
#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

extern uint64_t crc64(uint64_t crc, const void *buf, uint64_t size);

#ifdef __cplusplus
}
#endif
