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

#import "Rasterizer.hpp"

struct RasterizerWinding {
    struct Pair {
        Pair() : i0(INT_MAX), i1(INT_MAX) {}
        Pair(size_t i0, size_t i1) : i0(int(i0)), i1(int(i1)) {}
        int i0, i1;
    };
    
    static Ra::Vector<Pair> indicesForRect(Ra::SceneList& list, Ra::Bounds rect) {
        Ra::Vector<Pair> indices;
        for (size_t il = 0; il < list.scenes.size(); il++) {
            const Ra::Scene& scene = *list.scenes[il].ptr;
            const Ra::Transform ctm = list.ctms[il].concat(list.ctm);
            const Ra::Bounds& sceneclip = list.clips[il];
            
            for (size_t is = 0; is < scene.count(); is++) {
                const Ra::Draw& draw = scene.draws[is];
                if (draw.flags & Ra::Draw::kInvisible)
                    continue;
                if (list.params.useClips) {
                    if ((!sceneclip.isHuge() || !draw.clip.isHuge()) && !Winder::TouchesRect(rect, sceneclip.intersect(draw.clip), ctm))
                        continue;
                    if (draw.clipPath.ptr && !Winder::TouchesRect(rect, draw.clipPath.ptr, ctm, 0, 0))
                        continue;
                }
                const Ra::Transform m = draw.ctm.concat(ctm);
                const Ra::Bounds clip = draw.bnds.quad(m);
                if (clip.intersect(rect).isRect() && (rect.contains(clip) || Winder::TouchesRect(rect, draw.path.ptr, m, draw.width, draw.flags)))
                    indices.add(Pair(il, is));
            }
        }
        return indices;
    }
    
    struct Winder: Ra::GeometryWriter {
        static bool TouchesRect(Ra::Bounds rect, Ra::Geometry *g, Ra::Transform m, float width, uint8_t flags) {
            float ws = m.scale(), dw = width * (width < 0.f ? -1.f : ws);
            Winder winder;  winder.dw = 0.5f * dw, winder.flags = flags;
            winder.unit = rect.quad(Ra::Transform()).invert();
            winder.applyPath(g, m, rect.inset(-dw, -dw), false, width == 0.f);
            float cover = fabsf(winder.winding);
            return (flags & Ra::Draw::kFillEvenOdd ? 1.f - fabsf(fmodf(cover, 2.f) - 1.f) : cover) > 1e-3f;
        }
        
        static bool TouchesRect(Ra::Bounds rect, Ra::Bounds b, Ra::Transform ctm) {
            Winder winder;
            winder.unit = ctm.concat(rect.quad(Ra::Transform()).invert());
            winder.applyRect(b);
            return fabsf(winder.winding) > 1e-3f;
        }
        
        inline static float saturate(float t) {
            return fmaxf(0.f, fminf(1.f, t));
        }
        void applyRect(Ra::Bounds rect) {
            float x0, y0, x1, y1;
            x0 = x1 = rect.lx, y0 = y1 = rect.ly;
            for (size_t i = 1; i < 5; i++, x0 = x1, y0 = y1) {
                bool right = (i % 4) / 2, up = (i % 2) ^ right;
                x1 = (right ? rect.ux : rect.lx), y1 = (up ? rect.uy : rect.ly);
                winding += uwinding(x0, y0, x1, y1);
            }
        }
        inline float uwinding(float x0, float y0, float x1, float y1) {
            return awinding(
                fmaf(x0, unit.a, fmaf(y0, unit.c, unit.tx)),
                fmaf(x0, unit.b, fmaf(y0, unit.d, unit.ty)),
                fmaf(x1, unit.a, fmaf(y1, unit.c, unit.tx)),
                fmaf(x1, unit.b, fmaf(y1, unit.d, unit.ty))
            );
        }
        inline float awinding(float x0, float y0, float x1, float y1) {
            float w0 = saturate(y0), w1 = saturate(y1), cover = w1 - w0;
            float dx = x1 - x0, dy = y1 - y0, a0 = dx * ((dx > 0.f ? w0 : w1) - y0) - dy * (1.f - x0);
            return saturate(-a0 / fmaf(fabsf(dx), cover, dy)) * cover;
        }
        void writeSegment(float x0, float y0, float x1, float y1) {
            if (dw == 0)
                winding += uwinding(x0, y0, x1, y1);
            else {
                float ax, ay, dot, scale, cap, capx, capy, edgex, edgey, sx0, sy0, sx1, sy1;
                ax = x1 - x0, ay = y1 - y0, dot = ax * ax + ay * ay, scale = dw / sqrtf(dot);
                cap = scale * bool(flags & (Ra::Draw::kRoundCap | Ra::Draw::kSquareCap));
                capx = cap * ax, edgex = scale * -ay;
                capy = cap * ay, edgey = scale * ax;
                
                sx0 = sx1 = x0 - capx - edgex, sy0 = sy1 = y0 - capy - edgey;
                for (size_t i = 1; i < 5; i++, sx0 = sx1, sy0 = sy1) {
                    bool right = (i % 4) / 2, up = (i % 2) ^ right;
                    sx1 = (right ? x1 : x0) + (up ? 1 : -1) * edgex + (right ? 1 : -1) * capx;
                    sy1 = (right ? y1 : y0) + (up ? 1 : -1) * edgey + (right ? 1 : -1) * capy;
                    winding += uwinding(sx0, sy0, sx1, sy1);
                }
            }
        }
        float dw = 0, winding = 0;  uint8_t flags = 0;  Ra::Transform unit;
    };
};

typedef RasterizerWinding RaWnd;
