//
//  RasterizerTest.hpp
//  Rasterizer
//
//  Created by Nigel Barber on 14/03/2019.
//  Copyright © 2019 @mindbrix. All rights reserved.
//
#import "Rasterizer.hpp"

struct RasterizerTest {
    static void addTestPaths(Rasterizer::Scene& scene) {
        Rasterizer::Colorant color(0, 0, 0, 255);
        if (0) {
            Rasterizer::Path shape;
            shape.ref->addBounds(Rasterizer::Bounds(100.5, 100.5, 199.5, 199.5));
            scene.addPath(shape, Rasterizer::Transform(1.f, 0.f, 0.f, 1.f, 0.f, 0.f), color);
        }
        if (0) {
            scene.addPath(createPhyllotaxisPath(100000), Rasterizer::Transform(1.f, 0.f, 0.f, 1.f, 0.f, 0.f), color);
        }
        if (0) {
            writePhyllotaxisToScene(100000, scene);
        }
    }
    static void writePhyllotaxisToScene(size_t count, Rasterizer::Scene& scene) {
        Rasterizer::Colorant color(0, 0, 0, 255);
        Rasterizer::Path rect;
        rect.ref->addBounds(Rasterizer::Bounds(0.f, 0.f, 1.f, 1.f));
        const float sine = 0.675490294261524f, cosine = -0.73736887807832f;
        float vx = 1.f, vy = 0.f, x, y, s, t;
        for (int i = 0; i < count; i++) {
            s = sqrtf(i), t = float(i) / float(count);
            scene.addPath(rect, Rasterizer::Transform(1.f + t, 0.f, 0.f, 1.f + t, s * vx - 0.5f, s * vy - 0.5f), color);
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
            Rasterizer::Bounds b(*dst);
            shapes.ref->bounds.extend(b.lx, b.ly), shapes.ref->bounds.extend(b.ux, b.uy);
            x = vx * cosine + vy * -sine, y = vx * sine + vy * cosine;
            vx = x, vy = y;
        }
        shapes.ref->setShapesBounds(count);
        return shapes;
    }
    static Rasterizer::Path createBoundsShapes(Rasterizer::Scene& scene, bool circles) {
        Rasterizer::Path shapes;
        shapes.ref->addShapes(scene.paths.size());
        if (circles)
            memset(shapes.ref->circles, 0x01, shapes.ref->shapesCount * sizeof(bool));
        Rasterizer::Transform *dst = shapes.ref->shapes;
        for (int i = 0; i < scene.paths.size(); i++, dst++)
            *dst = scene.paths[i].ref->bounds.unit(scene.ctms[i]);
        shapes.ref->bounds = scene.bounds;
        return shapes;
    }
};
