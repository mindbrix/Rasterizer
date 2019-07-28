//
//  RasterizerTest.hpp
//  Rasterizer
//
//  Created by Nigel Barber on 14/03/2019.
//  Copyright © 2019 @mindbrix. All rights reserved.
//
#import "Rasterizer.hpp"

struct RasterizerTest {
    static void addTestScenes(Ra::SceneList& list) {
        Ra::Ref<Ra::Scene> scene;
        Ra::Colorant color(0, 0, 0, 255);
        if (0)
            scene.ref->addPath(Ra::boundsPath(Ra::Bounds(100.5, 100.5, 199.5, 199.5)), Ra::Transform(), color);
        if (0)
            scene.ref->addPath(createPhyllotaxisPath(1000), Ra::Transform(), color);
        if (0)
            writePhyllotaxisToScene(100000, *scene.ref);
        list.addScene(scene, Ra::Transform(), Ra::Transform::nullclip(), 0.f, true);
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
};
