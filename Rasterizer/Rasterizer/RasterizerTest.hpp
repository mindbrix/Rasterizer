//
//  RasterizerTest.hpp
//  Rasterizer
//
//  Created by Nigel Barber on 14/03/2019.
//  Copyright © 2019 @mindbrix. All rights reserved.
//
#import "Rasterizer.hpp"
#import "Rasterizer3D.hpp"
#import "RasterizerState.hpp"
#import <time.h>

struct RasterizerTest {
    void addTestScenes(Ra::Bounds b, RasterizerFont& font, Ra::SceneList& list) {
        Ra::Scene scene;
        Ra::Colorant black(0, 0, 0, 255), red(0, 0, 255, 255);
        if (0) {
            float w = 10, r = w;
            for (int i = 0; i < 20; i++, r += w) {
                Ra::Path circle;  circle.ref->addEllipse(Ra::Bounds(-r, -r, r, r));
                scene.addPath(circle, Ra::Transform(), i & 1 ? black : red, w, 0);
            }
        }
        if (0) {
            Ra::Bounds bounds(0, 0, 100, 100);
            Ra::Path rectPath; rectPath->addEllipse(bounds), rectPath->addEllipse(bounds.inset(20, 20));
            Ra::Path bbPath;  bbPath.ref->addEllipse(bounds);
            Ra::Path quadPath;  quadPath.ref->moveTo(100, 100), quadPath.ref->quadTo(0, 100, 00, 00);//, quadPath.ref->quadTo(100, 0, 0, 0);
            Ra::Path endsPath;  endsPath.ref->moveTo(0, 0), endsPath.ref->lineTo(0, 100);//, endsPath.ref->lineTo(1e-2, 100);//, endsPath.ref->quadTo(50, 110, 100, 100);
            
            if (1) {
                scene.addPath(rectPath, Ra::Transform(), black, 0.f, Ra::Scene::kFillEvenOdd);
//                scene.addPath(quadPath, Ra::Transform(), black, 0.f, 0);
            }
            else {
                float w = 10;
                scene.addPath(quadPath, Ra::Transform(), black, w, 0);
                scene.addPath(quadPath, Ra::Transform(1, 0, 0, 1, 0, w * 20), black, w, Ra::Scene::kOutlineEndCap);
                scene.addPath(quadPath, Ra::Transform(1, 0, 0, 1, 0, w * 40), black, w, Ra::Scene::kOutlineEndCap | Ra::Scene::kOutlineRounded);
                scene.addPath(endsPath, Ra::Transform(1, 0, 0, 1, 0, w * 60), black, w, Ra::Scene::kOutlineEndCap);// | Ra::Scene::kOutlineRounded);
            }
        }
        if (0) {
            float w = 400, inset = 1;
            Ra::Bounds bounds(0, 0, w, w);
            Ra::Path rectPath;  rectPath.ref->addBounds(bounds), rectPath.ref->addBounds(bounds.inset(20, 20));
            Ra::Path ellipsePath;
            for (int i = 0; i < 100; i++)
                ellipsePath.ref->addEllipse(bounds.inset(i * inset, i * inset));
            scene.addPath(ellipsePath, Ra::Transform(), black, 0.f, Ra::Scene::kFillEvenOdd);
            
        }
        if (0)
            scene.addPath(createPhyllotaxisPath(100), Ra::Transform(), black, 0.f, Ra::Scene::kFillEvenOdd);
        if (0)
            writePhyllotaxisToScene(100000, scene);
        list.addScene(scene);
        
        if (0) {
            float phi = 0.5f * (sqrtf(5.f) - 1.f);
            float size = 20.f, width = size * phi;
            list.addScene(createGridScene(10000, size, size * phi, width != 0.f, black));
        }
        if (0) {
            createConcentrichronScene(Ra::Bounds(0, 0, b.ux - b.lx, b.uy - b.ly), font, concentrichron.empty());
            bounds = b;
            writeList(list.empty());
        }
        if (0 && list.scenes.size()) {
            Ra::SceneList list3D;
            for (int i = 0; i < list.scenes.size(); i++)
                list3D.addScene(create3DScene(list.scenes[i]));
            list.empty().addList(list3D);
        }
        if (list.scenes.size())
            src.empty().addList(list);
    }
    
    static void writeConcentrichronList(Ra::SceneList& src, Ra::Bounds b, Ra::SceneList& list) {
        const float monthdays[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
        time_t t = time(NULL);
        struct tm *lt = localtime(& t);
        struct timeval tv;  gettimeofday(& tv, NULL);
        int year = lt->tm_year + 1900;
        bool isLeapYear = year % 4 == 0 && (year % 100 != 0 || year % 400 == 0);
        float fsec = (lt->tm_sec + tv.tv_usec / 1e6f) / 60.f, fmin = (lt->tm_min + fsec) / 60.f, fhour = (lt->tm_hour + fmin) / 24.f;
        float fday = (lt->tm_wday + fhour) / 7.f, fdate = (lt->tm_mday - 1 + fhour) / 31.f;
        float daysthismonth = monthdays[lt->tm_mon] + (lt->tm_mon == 1 && isLeapYear ? 1.f : 0.f);
        float fmonth = (lt->tm_mon + (lt->tm_mday - 1) / daysthismonth) / 12.f;
        float fyear = (lt->tm_year - 120 + (lt->tm_yday / (isLeapYear ? 365.f : 364.f))) / 10.f;
        float ftimes[8] = { 0, fyear, fmonth, fdate, fday, fhour, fmin, fsec };
        
        for (int i = 0; i < src.scenes.size(); i++)
            if (i > 0 && i < 8)
                list.addScene(src.scenes[i], Ra::Transform().concat(Ra::Transform::rst(ftimes[i] * 2.f * M_PI), 0.5f * (b.lx + b.ux), 0.5f * (b.ly + b.uy)));
            else
                list.addScene(src.scenes[i]);
    }
    static void createConcentrichronScene(Ra::Bounds b, RasterizerFont& font, Ra::SceneList& list) {
        const char *days[7] = { "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday" };
        const char *dates[31] = { "1st", "2nd", "3rd", "4th", "5th", "6th", "7th", "8th", "9th", "10th", "11th", "12th", "13th", "14th", "15th", "16th", "17th", "18th", "19th", "20th", "21st", "22nd", "23rd", "24th", "25th", "26th", "27th", "28th", "29th", "30th", "31st" };
        const char *months[12] = { "January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December" };
        const char *years[10] = { "2020", "2021", "2022", "2023", "2024", "2025", "2026", "2027", "2028", "2029" };
        const char **labels[8] = { NULL, years, months, dates, days, NULL, NULL, NULL };
        const int divisions[8] = { 0, 10, 12, 31, 7, 24, 60, 60 };
        const Ra::Colorant black(0, 0, 0, 255), red(0, 0, 255, 255), grey0(245, 245, 245, 255), grey1(250, 250, 250, 255);
        const float strokeWidth = 0.5f, arcWidth = 2.f;
        float w = b.ux - b.lx, h = b.uy - b.ly, dim = w < h ? w : h, inset = dim / 30.f;
        float cx = 0.5f * (b.lx + b.ux), cy = 0.5f * (b.ly + b.uy);
        Ra::Bounds outer = b.inset(0.5f * (w - dim + strokeWidth), 0.5f * (h - dim + strokeWidth));
        Ra::Scene background;
        for (int i = 1; i < 8; i++) {
            Ra::Bounds inner = outer.inset(inset * i, inset * i);
            Ra::Path bgPath;  bgPath->addEllipse(inner.inset(-inset * 0.5f, -inset * 0.5f));
            background.addPath(bgPath, Ra::Transform(), i % 2 ? grey0 : grey1, inset, 0);
        }
        Ra::Path ellipsePath; ellipsePath->addEllipse(outer);
        background.addPath(ellipsePath, Ra::Transform(), black, strokeWidth, 0);
        list.addScene(background);
        for (int i = 1; i < 8; i++) {
            Ra::Scene ring;
            Ra::Bounds inner = outer.inset(inset * i, inset * i);
            float step = 2.f * M_PI / divisions[i], theta0 = 0.5f * M_PI;
            float r0 = 0.5f * (inner.ux - inner.lx), r1 = r0 + (divisions[i] == 60 ? 0.25f : 0.5f) * inset;
            Ra::Path ellipsePath; ellipsePath->addEllipse(inner);
            ring.addPath(ellipsePath, Ra::Transform(), black, strokeWidth, 0);
            
            Ra::Path path;
            for (int j = 0; j < divisions[i]; j++) {
                float theta = theta0 + j * step;
                path->moveTo(cx + r0 * cosf(theta), cy + r0 * sinf(theta));
                path->lineTo(cx + r1 * cosf(theta), cy + r1 * sinf(theta));
            }
            ring.addPath(path, Ra::Transform(), black, strokeWidth, 0);
            
            if (!font.isEmpty()) {
                float r = r0 + 0.25f * inset, da, a0;
                Ra::Row<char> str;
                for (int j = 0; j < divisions[i]; j++) {
                    if (labels[i])
                        str = str.empty() + labels[i][j];
                    else
                        str = str.empty() + j;
                    Ra::Scene glyphs;  Ra::Bounds gb = RasterizerFont::writeGlyphs(font, inset * 0.666f, black, b, false, false, false, str.base, glyphs);
                    da = (gb.ux - gb.lx) / r, a0 = theta0 + j * -step - 0.5f * (step - da);
                    
                    if (0) {
                        Ra::Path arcPath;  arcPath->addArc(cx, cy, r0 + 0.5f * (strokeWidth + arcWidth), a0 - da, a0);
                        ring.addPath(arcPath, Ra::Transform(), red, arcWidth, 0);
                    }
                    RasterizerFont::writeGlyphsOnArc(glyphs, cx, cy, r, a0, ring);
                }
            }
            list.addScene(ring);
        }
        Ra::Scene line;
        Ra::Path linePath;  linePath->moveTo(cx, outer.uy - inset * 7.f), linePath->lineTo(cx, outer.uy);
        line.addPath(linePath, Ra::Transform(), red, 1.f, 0);
        list.addScene(line);
    }
    static Ra::Scene createGridScene(size_t count, float size, float width, bool outline, Ra::Colorant color) {
        Ra::Scene scene;
        size_t dim = ceilf(sqrtf(count)), i;
        for (i = 0; i < count; i++) {
            float lx = size * (i % dim), ly = size * (i / dim);
            Ra::Path path;
            if (outline)
                path.ref->moveTo(lx, ly + 0.5f * width), path.ref->lineTo(lx + width, ly + 0.5f * width);
            else
                path.ref->addBounds(Ra::Bounds(lx, ly, lx + width, ly + width));
            scene.addPath(path, Ra::Transform(), color, width, 0);
        }
        return scene;
    }
    static Ra::Path createPhyllotaxisPath(size_t count) {
        Ra::Path path;
        path.ref->moveTo(0.f, 0.f);
        const float sine = 0.675490294261524f, cosine = -0.73736887807832f;
        float vx = 1.f, vy = 0.f, x, y, s, t;
        for (int i = 0; i < count; i++) {
            s = sqrtf(i), t = float(i) / float(count);
            path.ref->lineTo(s * vx - 0.5f, s * vy - 0.5f);
            x = vx * cosine + vy * -sine, y = vx * sine + vy * cosine;
            vx = x, vy = y;
        }
        return path;
    }
    static void writePhyllotaxisToScene(size_t count, Ra::Scene& scene) {
        Ra::Colorant color(0, 0, 0, 255);
        Ra::Path line;
        line.ref->moveTo(0, 0), line.ref->lineTo(0, 1);
        const float sine = 0.675490294261524f, cosine = -0.73736887807832f;
        float vx = 1.f, vy = 0.f, x, y, s, t;
        for (int i = 0; i < count; i++) {
            s = sqrtf(i), t = float(i) / float(count);
            scene.addPath(line, Ra::Transform(1.f + t, 0.f, 0.f, 1.f + t, s * vx - 0.5f, s * vy - 0.5f), color, 1.f, 0);
            x = vx * cosine + vy * -sine, y = vx * sine + vy * cosine;
            vx = x, vy = y;
        }
    }
    
    void animate(Ra::SceneList& list, RasterizerState& state) {
        const Ra::Colorant black(0, 0, 0, 255), red(0, 0, 255, 255);
        const float kScaleMin = 1.0f, kScaleMax = 1.2f, kTxMin = 0.f, kTxMax = 0.f;
        float ftime = state.clock - floor(state.clock);
        float t = sinf(kTau * ftime), s = 1.f - t;
        float scale = s * kScaleMin + t * kScaleMax;
        float tx, ty, cx, cy;
        for (Ra::Scene *sb = & list.scenes[0], *ss = sb, *end = ss + list.scenes.size(); ss < end; ss++) {
            Ra::Transform *ctms = & ss->_ctms->src[0];
            Ra::Colorant *colors = & ss->_colors->src[0];
            float *widths = & ss->_widths->src[0];
            uint8_t *flags = & ss->_flags->src[0];
            for (int j = 0; j < ss->count; j++) {
                if (1) {
                    tx = s * kTxMin + t * kTxMax, ty = tx;
                    Ra::Transform rst = Ra::Transform::rst(M_PI * t * (j & 1 ? -1.f : 1.f), scale, scale);
                    Ra::Transform m = Ra::Transform(1.f, 0.f, 0.f, 1.f, tx, ty).concat(ctms[j]);
                    Ra::Bounds b = Ra::Bounds(ss->paths[j]->bounds.unit(m));
                    cx = 0.5f * (b.lx + b.ux), cy = 0.5f * (b.ly + b.uy);
                    ss->ctms[j] = m.concat(rst, cx, cy);
                }
                if (1) {
                    ss->widths[j] = scale * widths[j];
                }
                if (1) {
                    ss->colors[j] = (state.indices.begin == (ss - sb) && state.indices.end == j) ? red : state.outlineWidth != 0.f ? black : colors[j];
                }
                if (1) {
                    ss->flags[j] = (flags[j] & ~Ra::Scene::kInvisible) | (state.locked.begin == INT_MAX || (ss - sb == state.locked.begin && j == state.locked.end) ? 0 : Ra::Scene::kInvisible);
                }
            }
        }
    }
    void readEvents(Ra::SceneList& list, RasterizerState& state) {
        if (src.pathsCount) {
            animate(src, state), writeList(list.empty());
        }
    }
    void writeList(Ra::SceneList& list) {
        if (concentrichron.pathsCount)
            writeConcentrichronList(concentrichron, bounds, list);
        else if(src.pathsCount)
            list.addList(src);
    }
    size_t refCount = 0;
    
    Ra::SceneList concentrichron;  Ra::Bounds bounds;
    
    Ra::SceneList src;
};
