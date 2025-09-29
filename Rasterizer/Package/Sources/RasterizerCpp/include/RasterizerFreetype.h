//
//  Copyright 2025 Nigel Timothy Barber - nigel@mindbrix.co.uk
//
//  This software is provided 'as-is', without any express or implied
//  warranty. In no event will the authors be held liable for any damages
//  arising from the use of this software.
//
//  Permission is granted to anyone to use this software for personal use
//  (for a commercial licence please contact the author), and to alter it and
//  redistribute it freely, subject to the following restrictions:
//
//  1. The origin of this software must not be misrepresented; you must not
//  claim that you wrote the original software. If you use this software
//  in a product, an acknowledgment in the product documentation would be
//  appreciated but is not required.
//  2. Altered source versions must be plainly marked as such, and must not be
//  misrepresented as being the original software.
//  3. This notice may not be removed or altered from any source distribution.
//
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_OUTLINE_H
#include FT_BBOX_H

struct RasterizerFreetype {
    static int MoveToFunction(const FT_Vector *to, void *user) {
        (*(Ra::Path *)user)->moveTo(to->x, to->y);
        return 0;
    }
    static int LineToFunction(const FT_Vector *to, void *user) {
        (*(Ra::Path *)user)->lineTo(to->x, to->y);
        return 0;
    }
    static int ConicToFunction(const FT_Vector *control,
                             const FT_Vector *to,
                             void *user) {
        (*(Ra::Path *)user)->quadTo(control->x, control->y, to->x, to->y);
        return 0;
    }
    static int CubicToFunction(const FT_Vector *controlOne,
                             const FT_Vector *controlTwo,
                             const FT_Vector *to,
                             void *user) {
        (*(Ra::Path *)user)->cubicTo(controlOne->x, controlOne->y, controlTwo->x, controlTwo->y, to->x, to->y);
        return 0;
    }
    RasterizerFreetype() {
        error = FT_Init_FreeType(& library);
    }
    void loadFace(Ra::Vector<unsigned char> data) {
        error = FT_New_Memory_Face(library, & data[0], data.size(), 0, & face);
        faceData = data;
    }
    Ra::Path createCharPath(size_t code) {
        Ra::Path path;
        error = FT_Load_Char(face, code, 0);
        if (error == 0) {
            assert(face->glyph->format == FT_GLYPH_FORMAT_OUTLINE);
            FT_Outline outline = face->glyph->outline;
            FT_Outline_Funcs callbacks;
            callbacks.move_to = MoveToFunction;
            callbacks.line_to = LineToFunction;
            callbacks.conic_to = ConicToFunction;
            callbacks.cubic_to = CubicToFunction;
            callbacks.shift = 0;
            callbacks.delta = 0;
            error = FT_Outline_Decompose(& outline, & callbacks, & path);
        }
        return path;
    }
    ~RasterizerFreetype() {
        if (face)
            FT_Done_Face(face);
        if (library)
            FT_Done_FreeType(library);
    }
    FT_Library    library = nullptr;
    FT_Error      error = 0;
    FT_Face       face = nullptr;
    Ra::Vector<unsigned char> faceData;
};
