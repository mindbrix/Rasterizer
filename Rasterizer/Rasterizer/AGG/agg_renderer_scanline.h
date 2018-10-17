//----------------------------------------------------------------------------
// Anti-Grain Geometry - Version 2.4
// Copyright (C) 2002-2005 Maxim Shemanarev (http://www.antigrain.com)
//
// Permission to copy, use, modify, sell and distribute this software 
// is granted provided this copyright notice appears in all copies. 
// This software is provided "as is" without express or implied
// warranty, and with no claim as to its suitability for any purpose.
//
//----------------------------------------------------------------------------
// Contact: mcseem@antigrain.com
//          mcseemagg@yahoo.com
//          http://www.antigrain.com
//----------------------------------------------------------------------------

#ifndef AGG_RENDERER_SCANLINE_INCLUDED
#define AGG_RENDERER_SCANLINE_INCLUDED

#include "agg_basics.h"
#import "color-mask.h"

#import "VGFloatGeometry.hpp"

namespace agg
{

    //--------------------------------------------------------------------
    template<class Accessor>
    void blend_hline(Accessor& accessor,
                     int x, int y,
                     int len,
                     uint8_t red, uint8_t green, uint8_t blue, uint8_t alpha,
                     int8u cover) {
        if (alpha) {
            uint32_t color = alpha << 24 | red << 16 | green << 8 | blue;
            
            auto is_opaque = alpha == 255;
            auto p = accessor.pixel_ptr(x, y, len, 1);
            
            if (is_opaque && cover == cover_mask) {
                memset_pattern4(p, & color, len << 2);
            } else {
                auto covers = (const uint8_t *)alloca(len);
                uint8_t cover_pattern[] = { cover, cover, cover, cover };
                memset_pattern4((void *)covers, cover_pattern, len);
                
                if (is_opaque)
                    CGSColorMaskCopyARGB8888(color, (uint32_t *)p, 0, covers, 0, len, 1);
                else
                    CGSColorMaskSoverARGB8888(color, (uint32_t *)p, 0, covers, 0, len, 1);
            }
        }
    }
    
    
    //--------------------------------------------------------------------
    template<class Accessor>
    void blend_solid_hspan(Accessor& accessor,
                           int x, int y,
                           int len, uint8_t red, uint8_t green, uint8_t blue, uint8_t alpha,
                           const int8u* covers) {
        if (alpha) {
            uint32_t color = alpha << 24 | red << 16 | green << 8 | blue;
            
            auto p = accessor.pixel_ptr(x, y, len, 1);
            
            if (alpha == 255)
                CGSColorMaskCopyARGB8888(color, (uint32_t *)p, 0, (const uint8_t *)covers, 0, len, 1);
            else
                CGSColorMaskSoverARGB8888(color, (uint32_t *)p, 0, (const uint8_t *)covers, 0, len, 1);
        }
    }

    
    //================================================render_scanline_aa_solid
    template<class Scanline, class Accessor>
    void render_scanline_aa_solid(const Scanline& sl, Accessor& accessor, uint8_t red, uint8_t green, uint8_t blue, uint8_t alpha)
    {
        int y = sl.y();
        unsigned num_spans = sl.num_spans();
        typename Scanline::const_iterator span = sl.begin();

        for(;;)
        {
            int x = span->x;
            if(span->len > 0) {
                blend_solid_hspan(accessor, x, y, span->len,
                                      red, green, blue, alpha,
                                      span->covers);
            } else {
                blend_hline(accessor, x, y, -span->len,
                                red, green, blue, alpha,
                                *(span->covers));
            }
            if(--num_spans == 0) break;
            ++span;
        }
    }

    template<class Rasterizer, class Scanline, class Accessor>
    void render_scanlines_aa(Rasterizer& ras, Scanline& sl, Accessor& accessor, uint8_t red, uint8_t green, uint8_t blue, uint8_t alpha)
    {
        if(ras.rewind_scanlines()) {
            sl.reset(ras.min_x(), ras.max_x());
            while(ras.sweep_scanline(sl)) {
                render_scanline_aa_solid(sl, accessor, red, green, blue, alpha);
            }
        }
    }
    
    template<class Scanline, class Vertices>
    void render_scanline_aa_vertices(const Scanline& sl, Vertices& vertices, VGAffineTransform& deviceTransform) {
        int y = sl.y();
        unsigned num_spans = sl.num_spans();
        typename Scanline::const_iterator span = sl.begin();
        
        for(;;) {
            int x1 = span->x;
            if (span->len > 0) {
                int x2 = x1 + span->len;
                auto covers = span->covers;
                for (int x = x1; x != x2; x++) {
                    uint32_t color = *covers++ << 24;
                    addSpan(vertices, x, x + 1, y, color, deviceTransform);
                }
            } else {
                int x2 = x1 - span->len;
                uint32_t cover = *(span->covers) << 24;
                addSpan(vertices, x1, x2, y, cover, deviceTransform);
            }

            if (--num_spans == 0) break;
            ++span;
        }
    }
    
    //===============================================render_mask_aa_vertices
    template<class Rasterizer, class Vertices>
    void render_mask_aa_vertices(Rasterizer& ras, Vertices& vertices, VGAffineTransform deviceTransform) {
        auto mask = ras.make_mask();
        
        auto y = ras.min_y(), h = ras.height();
        
        while (h--) {
            auto x = ras.min_x(), w = ras.width();
            int x1 = 0;
            uint8_t lastCover = 0;
            
            while (w--) {
                uint8_t cover = *mask++;
                
                if (cover != lastCover) {
                    if (lastCover)
                        addSpan(vertices, x1, x, y, lastCover << 24, deviceTransform);
                    lastCover = cover;
                    x1 = x;
                }
                
//                if (cover)
//                    addSpan(vertices, x, x + 1, y, cover << 24, deviceTransform);
                
                x++;                
            }
            
            if (lastCover)
                addSpan(vertices, x1, x, y, lastCover << 24, deviceTransform);
            
            y++;
        }
    }
    
    
    //===============================================render_scanlines_aa_vertices
    template<class Rasterizer, class Scanline, class Vertices>
    void render_scanlines_aa_vertices(Rasterizer& ras, Scanline& sl, Vertices& vertices, VGAffineTransform deviceTransform) {
        if (ras.rewind_scanlines()) {
            sl.reset(ras.min_x(), ras.max_x());
            while(ras.sweep_scanline(sl)) {
                render_scanline_aa_vertices(sl, vertices, deviceTransform);
            }
        }
    }
    
    
    //===============================================render_mask_aa_solid
    template<class Rasterizer, class Accessor>
    void render_mask_aa_solid(Rasterizer& ras, Accessor& accessor, uint8_t red, uint8_t green, uint8_t blue, uint8_t alpha) {
        auto mask = ras.makeMask();
        
        if (alpha) {
            uint32_t color = alpha << 24 | red << 16 | green << 8 | blue;
            auto x = ras.min_x(), y = ras.min_y(), w = ras.width(), h = ras.height();
            
            auto p = accessor.pixel_ptr(x, y, w, h);
            
            if (alpha == 255)
                CGSColorMaskCopyARGB8888(color, (uint32_t *)p, accessor.stride(), (const uint8_t *)mask, w, w, h);
            else
                CGSColorMaskSoverARGB8888(color, (uint32_t *)p, accessor.stride(), (const uint8_t *)mask, w, w, h);
        }
    }
}

#endif
