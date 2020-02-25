//
//  RasterizerTest.hpp
//  Rasterizer
//
//  Created by Nigel Barber on 14/03/2019.
//  Copyright © 2019 @mindbrix. All rights reserved.
//
#import "Rasterizer.hpp"

struct RasterizerTest {
    static void addTestScenes(Ra::SceneList& list, RasterizerFont& font) {
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
        list.addScene(scene, Ra::Transform(), Ra::Transform::nullclip());
        
        if (0) {
            float phi = 0.5f * (sqrtf(5.f) - 1.f);
            float size = 20.f, width = size * phi;
            list.addScene(createGridScene(10000, size, size * phi, width != 0.f, black), Ra::Transform(), Ra::Transform::nullclip());
        }
        if (1)
            list.addScene(createConcentrichronScene(Ra::Bounds(0, 0, 800, 600), font), Ra::Transform(), Ra::Transform::nullclip());
    }
    static Ra::Scene createConcentrichronScene(Ra::Bounds b, RasterizerFont& font) {
        Ra::Colorant black(0, 0, 0, 255), red(0, 0, 255, 255);
        Ra::Scene scene;
        float w = b.ux - b.lx, h = b.uy - b.ly, dim = w < h ? w : h, inset = dim * 0.0333f;
        float cx = 0.5f * (b.lx + b.ux), cy = 0.5f * (b.ly + b.uy);
        Ra::Bounds outer = b.inset(0.5f * (w - dim), 0.5f * (h - dim));
        Ra::Path ellipsePath; ellipsePath->addEllipse(outer);
        scene.addPath(ellipsePath, Ra::Transform(), black, 1.f, 0);
        int counts[] = { 0, 10, 12, 31, 7, 24, 60, 60 };
        const char *days[] = { "Sunday", "Monday", "Tueday", "Wednesday", "Thursday", "Friday", "Saturday" };
        const char *dates[] = { "1st", "2nd", "3rd", "4th", "5th", "6th", "7th", "8th", "9th", "10th", "11th", "12th", "13th", "14th", "15th", "16th", "17th", "18th", "19th", "20th", "21st", "22nd", "23rd", "24th", "25th", "26th", "27th", "28th", "29th", "30th", "31st" };
        const char *months[] = { "January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December" };
        const char *years[] = { "2020", "2021", "2022", "2023", "2024", "2025", "2026", "2027", "2028", "2029" };
        const char **labels[] = { NULL, years, months, dates, days, NULL, NULL, NULL };
        for (int i = 1; i < 8; i++) {
            Ra::Bounds inner = outer.inset(inset * i, inset * i);
            float r0 = 0.5f * (inner.ux - inner.lx), r1 = r0 + 0.5f * inset;
            Ra::Path ellipsePath; ellipsePath->addEllipse(inner);
            scene.addPath(ellipsePath, Ra::Transform(), black, 1.f, 0);
            
            Ra::Path path;
            int steps = counts[i];
            for (int j = 0; j < steps; j++) {
                float theta = j * 2.f * M_PI / steps;
                path->moveTo(cx + r0 * cosf(theta), cy + r0 * sinf(theta));
                path->lineTo(cx + r1 * cosf(theta), cy + r1 * sinf(theta));
            }
            scene.addPath(path, Ra::Transform(), black, 1.f, 0);
            
            Ra::Row<char> str;
            for (int j = 0; j < steps; j++) {
                if (labels[i])
                    str = str.empty() + labels[i][j];
                else
                    str = str.empty() + j;
                Ra::Scene glyphs;  Ra::Bounds gb = RasterizerFont::writeGlyphs(font, inset * 0.666f, black, b, false, false, false, str.base, glyphs);
                float r = 0.5f * (r0 + r1), range = (gb.ux - gb.lx) / r, step = 2.f * M_PI / steps, offset = 0.5f * (step - range);
                addGlyphsOnArc(glyphs, cx, cy, r, j * -step - offset, scene);
            }
        }
        return scene;
    }
    static void addGlyphsOnArc(Ra::Scene& glyphs, float cx, float cy, float r, float theta, Ra::Scene& scene) {
        Ra::Path path;  Ra::Transform m;  Ra::Bounds b;  float lx = 0.f;
        for (int i = 0; i < glyphs.count; i++) {
            path = glyphs.paths[i], m = glyphs.ctms[i], b = Ra::Bounds(path->bounds.unit(m));
            lx = i == 0 ? b.lx : lx;
            float bx = 0.5f * (b.lx + b.ux), by = m.ty, rot = theta - (bx - lx) / r;
            float px = cx + r * cosf(rot), py = cy + r * sinf(rot);
            Ra::Transform ctm = m.concat(Ra::Transform::rotation(rot - 0.5 * M_PI), bx, by);
            scene.addPath(path, Ra::Transform(1, 0, 0, 1, px - bx, py - by).concat(ctm), glyphs.colors[i], 0.f, 0);
        }
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
};
