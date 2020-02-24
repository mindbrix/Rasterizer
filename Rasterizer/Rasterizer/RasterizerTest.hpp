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
        Ra::Scene glyphs;  RasterizerFont::writeGlyphs(font, inset, red, b, false, false, false, "y", glyphs);
        Ra::Bounds outer = b.inset(0.5f * (w - dim), 0.5f * (h - dim));
        Ra::Path ellipsePath; ellipsePath->addEllipse(outer);
        scene.addPath(ellipsePath, Ra::Transform(), black, 1.f, 0);
        int counts[] = { 0, 10, 12, 31, 7, 24, 60, 60 };
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
            
            for (int j = 0; j < steps; j++) {
                addGlyphOnArc(glyphs, red, cx, cy, 0.5f * (r0 + r1), j * 2.f * M_PI / steps, scene);
            }
        }
        return scene;
    }
    static void addGlyphOnArc(Ra::Scene& glyphs, Ra::Colorant color, float cx, float cy, float r, float theta, Ra::Scene& scene) {
        Ra::Path p8;  Ra::Transform m8;
        if (glyphs.count)
            p8 = glyphs.paths[0], m8 = glyphs.ctms[0];
        Ra::Bounds b8 = Ra::Bounds(p8->bounds.unit(m8));
        float b8x = 0.5f * (b8.lx + b8.ux), b8y = m8.ty;
        float px = cx + r * cosf(theta), py = cy + r * sinf(theta);
        Ra::Transform ctm = m8.concat(Ra::Transform::rotation(theta - 0.5 * M_PI), b8x, b8y);
        scene.addPath(p8, Ra::Transform(1, 0, 0, 1, px - b8x, py - b8y).concat(ctm), color, 0.f, 0);
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
