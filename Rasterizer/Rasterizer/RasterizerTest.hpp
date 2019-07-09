//
//  RasterizerTest.hpp
//  Rasterizer
//
//  Created by Nigel Barber on 14/03/2019.
//  Copyright © 2019 @mindbrix. All rights reserved.
//
#import "Rasterizer.hpp"

struct RasterizerTest {
    static void addTestPaths(Ra::Scene& scene) {
        Ra::Colorant color(0, 0, 0, 255);
        if (0)
            scene.addPath(Ra::boundsPath(Ra::Bounds(100.5, 100.5, 199.5, 199.5)), Ra::Transform(), color);
        if (0)
            scene.addPath(createPhyllotaxisPath(100000), Ra::Transform(), color);
        if (0)
            writePhyllotaxisToScene(100000, scene);
        if (0)
            scene.addPath(createBoundsShape(Ra::Bounds(100, 100, 200, 800)), Ra::Transform(), color);
    }
    static void writePhyllotaxisToScene(size_t count, Ra::Scene& scene) {
        Ra::Colorant color(0, 0, 0, 255);
        Ra::Path rect;
        rect.ref->addBounds(Ra::Bounds(0.f, 0.f, 1.f, 1.f));
        const float sine = 0.675490294261524f, cosine = -0.73736887807832f;
        float vx = 1.f, vy = 0.f, x, y, s, t;
        for (int i = 0; i < count; i++) {
            s = sqrtf(i), t = float(i) / float(count);
            scene.addPath(rect, Ra::Transform(1.f + t, 0.f, 0.f, 1.f + t, s * vx - 0.5f, s * vy - 0.5f), color);
            x = vx * cosine + vy * -sine, y = vx * sine + vy * cosine;
            vx = x, vy = y;
        }
    }
    static Ra::Path createPhyllotaxisPath(size_t count) {
        Ra::Path shapes;
        shapes.ref->allocShapes(count);
        Ra::Transform *dst = shapes.ref->shapes;
        const float sine = 0.675490294261524f, cosine = -0.73736887807832f;
        float vx = 1.f, vy = 0.f, x, y, s;
        for (int i = 0; i < count; i++, dst++) {
            shapes.ref->circles[i] = i & 1;
            s = sqrtf(i);
            new (dst) Ra::Transform(1.f, 0.f, 0.f, 1.f, s * vx - 0.5f, s * vy - 0.5f);
            x = vx * cosine + vy * -sine, y = vx * sine + vy * cosine;
            vx = x, vy = y;
        }
        shapes.ref->updateShapes(count);
        return shapes;
    }
    static Ra::Path createBoundsShapes(Ra::Scene& scene, bool circles) {
        Ra::Path shapes;
        shapes.ref->allocShapes(scene.paths.size());
        if (circles)
            memset(shapes.ref->circles, 0x01, shapes.ref->shapesCount * sizeof(bool));
        Ra::Transform *dst = shapes.ref->shapes;
        for (int i = 0; i < scene.paths.size(); i++, dst++)
            *dst = scene.paths[i].ref->bounds.unit(scene.ctms[i]);
        shapes.ref->bounds = scene.bounds;
        return shapes;
    }
    static Ra::Path createBoundsShape(Ra::Bounds b) {
        Ra::Path shape;
        shape.ref->allocShapes(1);
        shape.ref->shapes[0] = Ra::Transform(b.ux - b.lx, 0.f, 0.f, b.uy - b.ly, b.lx, b.ly);
        shape.ref->circles[0] = true;
        shape.ref->bounds = b;
        return shape;
    }
};
