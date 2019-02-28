//
//  crc32.h
//  Rasterizer
//
//  Created by Nigel Barber on 28/02/2019.
//  Copyright © 2019 @mindbrix. All rights reserved.
//
#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

extern uint32_t crc32(uint32_t crc, const void *buf, size_t size);

#ifdef __cplusplus
}
#endif
