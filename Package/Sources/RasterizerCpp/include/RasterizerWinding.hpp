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
    struct IndexPair {
        IndexPair() : i0(INT_MAX), i1(INT_MAX) {}
        IndexPair(size_t i0, size_t i1) : i0(int(i0)), i1(int(i1)) {}
        int i0, i1;
    };
    
    static IndexPair indicesForPoint(Ra::SceneList& list, Ra::Bounds bounds, float px, float py) {
        if (px >= bounds.lx && px < bounds.ux && py >= bounds.ly && py < bounds.uy)
            for (int il = int(list.scenes.size()) - 1; il >= 0; il--) {
                Ra::Scene& scene = *list.scenes[il].ptr;
                Ra::Transform ctm = list.ctms[il].concat(list.ctm), inv;
                Ra::Bounds sceneclip = list.clips[il], lastClip;
                for (int is = int(scene.count()) - 1; is >= 0; is--) {
                    Ra::Draw& draw = scene.draws[is];
                    if (draw.flags & Ra::Draw::kInvisible)
                        continue;
                    if (memcmp(& draw.clip, & lastClip, sizeof(Ra::Bounds)) != 0) {
                        lastClip = draw.clip;
                        inv = sceneclip.intersect(draw.clip).quad(ctm).invert();
                    }
                    float ux = inv.a * px + inv.c * py + inv.tx, uy = inv.b * px + inv.d * py + inv.ty;
                    bool inClipRect = !list.params.useClips || fmaxf(fabsf(ux - 0.5f), fabsf(uy - 0.5f)) <= 0.5f;
                    if (inClipRect) {
                        bool insideClipPath = !list.params.useClips || draw.clipPath.ptr == nullptr || pointInside(px, py, draw.clipPath.ptr, ctm, 0, 0);
                        if (insideClipPath) {
                            bool inside = pointInside(px, py, draw.path.ptr, draw.ctm.concat(ctm), draw.width, draw.flags);
                            if (inside)
                                return IndexPair(il, is);
                        }
                    }
                }
            }
        return IndexPair();
    }
    
    static Ra::Vector<IndexPair> indicesForRect(Ra::SceneList& list, Ra::Bounds rect) {
        Ra::Vector<IndexPair> indices;
        for (size_t il = 0; il < list.scenes.size(); il++) {
            const Ra::Scene& scene = *list.scenes[il].ptr;
            const Ra::Transform ctm = list.ctms[il].concat(list.ctm);
            
            for (size_t is = 0; is < scene.count(); is++) {
                const Ra::Draw& draw = scene.draws[is];
                if (draw.flags & Ra::Draw::kInvisible)
                    continue;
                const Ra::Transform m = draw.ctm.concat(ctm);
                const Ra::Bounds clip = draw.bnds.quad(m);
                if (clip.intersect(rect).isRect() && (rect.contains(clip) || Winder::TouchesRect(rect, draw.path.ptr, m, draw.width, draw.flags)))
                    indices.add(IndexPair(il, is));
            }
        }
        return indices;
    }
    
    static bool pointInside(float px, float py, Ra::Geometry *g, Ra::Transform m, float w, uint8_t flags) {
        float ws = m.scale(), uw = w < 0.f ? -w / ws : w;
        Ra::Transform u = g->bounds.inset(-uw, -uw).quad(m);
        float vx = px - u.tx, vy = py - u.ty;
        float t0 = (vx * u.a + vy * u.b) / (u.a * u.a + u.b * u.b);
        float t1 = (vx * u.c + vy * u.d) / (u.c * u.c + u.d * u.d);
        bool inBounds = fmaxf(fabsf(t0 - 0.5f), fabsf(t1 - 0.5f)) <= 0.5f;
        if (!inBounds)
            return false;
        
        Counter cntr;  cntr.dx = px, cntr.dy = py, cntr.dw = w * (w < 0.f ? -1.f : ws), cntr.flags = flags;
        cntr.quadraticScale = cntr.cubicScale = 1.f;
        Ra::Bounds clip = Ra::Bounds(u);
        bool polygon = w == 0.f;
        cntr.applyPath(g, m, clip, false, polygon);
        int mask = flags & Ra::Draw::kFillEvenOdd ? 1 : ~0;
        return cntr.winding & mask;
    }
    
    struct Winder: Ra::GeometryWriter {
        static bool TouchesRect(Ra::Bounds rect, Ra::Geometry *g, Ra::Transform m, float width, uint8_t flags) {
            Ra::Transform unit = m.concat(rect.quad(Ra::Transform()).invert());
            float ws = unit.scale(), dw = width * (width < 0.f ? -1.f : ws);
            Winder winder;  winder.dw = width == 0 ? 0 : 0.5f * dw, winder.flags = flags;
            winder.applyPath(g, unit, Ra::Bounds(), true, width == 0);
            return fabsf(winder.winding) > 1e-3f;
        }
        
        inline static float saturate(float t) {
            return fmaxf(0.f, fminf(1.f, t));
        }
        float awinding(float x0, float y0, float x1, float y1) {
            float w0 = saturate(y0), w1 = saturate(y1), cover = w1 - w0;
            float dx = x1 - x0, dy = y1 - y0, a0 = dx * ((dx > 0.0 ? w0 : w1) - y0) - dy * (1.0 - x0);
            return saturate(-a0 / fmaf(fabsf(dx), cover, dy)) * cover;
        }
        void writeSegment(float x0, float y0, float x1, float y1) {
            if (dw == 0)
                winding += awinding(x0, y0, x1, y1);
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
                    winding += awinding(sx0, sy0, sx1, sy1);
                }
            }
        }
        float dw = 0, winding = 0;  uint8_t flags = 0;
    };
    struct Counter: Ra::GeometryWriter {
        float dx, dy, dw;  int winding = 0;  uint8_t flags = 0;
        
        void writeSegment(float x0, float y0, float x1, float y1) {
            if (dw) {
                if (winding == 0 && (x0 != x1 || y0 != y1)) {
                    float ax, ay, adot, len, bx, by, cx, cy, t, s, sx, sy, cap;
                    ax = x1 - x0, ay = y1 - y0, adot = ax * ax + ay * ay, bx = dx - x0, by = dy - y0;
                    if (flags & Ra::Draw::kRoundCap) {
                        t = (ax * bx + ay * by) / adot, t = fmaxf(0.f, fminf(1.f, t)), s = 1.f - t;
                        cx = s * x0 + t * x1 - dx, cy = s * y0 + t * y1 - dy;
                        winding = (cx * cx + cy * cy) < 0.25f * dw * dw;
                    } else {
                        len = sqrtf(adot), sx = (ax * bx + ay * by) / len, sy = (ax * by - ay * bx) / len;
                        cap = flags & Ra::Draw::kSquareCap ? 0.5f * dw : 0.f;
                        winding = sx > -cap && sx < len + cap && fabsf(sy) < 0.5f * dw;
                    }
                }
            } else {
                if (dy >= (y0 < y1 ? y0 : y1) && dy < (y0 > y1 ? y0 : y1)) {
                    float det = (x1 - x0) * (dy - y0) - (y1 - y0) * (dx - x0);
                    if (y0 < y1 && det < 0.f)
                        winding++;
                    else if (y0 > y1 && det > 0.f)
                        winding--;
                }
            }
        }
    };
};

typedef RasterizerWinding RaWnd;
