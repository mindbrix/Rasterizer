//
//  RasterizerTest.hpp
//  Rasterizer
//
//  Created by Nigel Barber on 14/03/2019.
//  Copyright © 2019 @mindbrix. All rights reserved.
//
#import "Rasterizer.hpp"

struct RasterizerTest {
    static void writePhyllotaxisToScene(size_t count, RasterizerScene::Scene& scene) {
        uint8_t bgra[4] = { 0, 0, 0, 255 };
        Rasterizer::Path rect;
        rect.ref->addBounds(Rasterizer::Bounds(0.f, 0.f, 1.f, 1.f));
        const float sine = 0.675490294261524f, cosine = -0.73736887807832f;
        float vx = 1.f, vy = 0.f, x, y, s, t;
        for (int i = 0; i < count; i++) {
            s = sqrtf(i), t = float(i) / float(count);
            scene.addPath(rect, Rasterizer::Transform(1.f + t, 0.f, 0.f, 1.f + t, s * vx - 0.5f, s * vy - 0.5f), bgra);
            x = vx * cosine + vy * -sine, y = vx * sine + vy * cosine;
            vx = x, vy = y;
        }
    }
    static Rasterizer::Path createPhyllotaxisPath(size_t count) {
        Rasterizer::Path shapes;
        shapes.ref->addShapes(count);
        Rasterizer::Transform *dst = shapes.ref->shapes;
        const float sine = 0.675490294261524f, cosine = -0.73736887807832f;
        float vx = 1.f, vy = 0.f, x, y, s;
        for (int i = 0; i < count; i++, dst++) {
            shapes.ref->circles[i] = i & 1;
            s = sqrtf(i);
            new (dst) Rasterizer::Transform(1.f, 0.f, 0.f, 1.f, s * vx - 0.5f, s * vy - 0.5f);
            x = vx * cosine + vy * -sine, y = vx * sine + vy * cosine;
            vx = x, vy = y;
        }
        return shapes;
    }
};
