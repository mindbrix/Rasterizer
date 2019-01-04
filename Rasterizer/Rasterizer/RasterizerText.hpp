//
//  RasterizerText.hpp
//  Rasterizer
//
//  Created by Nigel Barber on 04/01/2019.
//  Copyright © 2019 @mindbrix. All rights reserved.
//

#import "Rasterizer.hpp"

#define STB_TRUETYPE_IMPLEMENTATION
#import "stb_truetype.h"

struct RasterizerText {
    static void writeGlyphPath(stbtt_fontinfo& font, int glyph, Rasterizer::Path& path) {
        if (stbtt_IsGlyphEmpty(& font, glyph) == 0) {
            stbtt_vertex *vertices, *vertex;
            int nverts, i;
            nverts = stbtt_GetGlyphShape(& font, glyph, & vertices);
            if (nverts) {
                for (vertex = vertices, i = 0; i < nverts; i++, vertex++) {
                    switch (vertex->type) {
                        case STBTT_vmove:
                            path.sequence->moveTo(vertex->x, vertex->y);
                            break;
                        case STBTT_vline:
                            path.sequence->lineTo(vertex->x, vertex->y);
                            break;
                        case STBTT_vcurve:
                            path.sequence->quadTo(vertex->x, vertex->y, vertex->cx, vertex->cy);
                            break;
                        case STBTT_vcubic:
                            path.sequence->cubicTo(vertex->x, vertex->y, vertex->cx, vertex->cy, vertex->cx1, vertex->cy1);
                            break;
                    }
                }
                stbtt_FreeShape(& font, vertices);
            }
        }
    }
};
