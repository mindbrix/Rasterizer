//
//  RasterizerEvent.hpp
//  Rasterizer
//
//  Created by Nigel Barber on 20/03/2019.
//  Copyright © 2019 @mindbrix. All rights reserved.
//
#import "Rasterizer.hpp"

struct RasterizerEvent {
    struct Event {
        enum Flags { kCapsLock = 1 << 16, kShift = 1 << 17, kControl = 1 << 18, kOption = 1 << 19, kCommand = 1 << 20, kNumericPad         = 1 << 21, kHelp = 1 << 22, kFunction = 1 << 23 };
        enum Type { kNull = 0, kMouseMove, kMouseUp, kMouseDown, kFlags, kKeyDown, kKeyUp, kMagnify, kRotate, kTranslate };
        
        Event() {}
        Event(double time, Type type, float x, float y) : time(time), type(type), x(x), y(y), keyCode(0) {}
        Event(double time, Type type, int keyCode) : time(time), type(type), x(0.f), y(0.f), keyCode(keyCode) {}
        Event(double time, Type type, size_t flags) : time(time), type(type), x(0.f), y(0.f), keyCode(0), flags(flags) {}
        
        double time;
        Type type;
        float x, y;
        int keyCode;
        size_t flags;
    };
    
    struct State {
        void update(float s, float w, float h) {
            scale = s;
            view = Rasterizer::Transform(s, 0.f, 0.f, s, 0.f, 0.f).concat(ctm);
            bounds = Rasterizer::Bounds(0.f, 0.f, w, h), device = Rasterizer::Bounds(0.f, 0.f, ceilf(s * w), ceilf(h * s));
        }
        bool keyDown = false, mouseDown = false, mouseMove = false, useClip = false, useOutline = false;
        float x, y, scale;
        int keyCode = 0;
        size_t index = INT_MAX, flags = 0;
        std::vector<RasterizerEvent::Event> events;
        Rasterizer::Transform ctm = { 1.f, 0.f, 0.f, 1.0, 0.f, 0.f }, view;
        Rasterizer::Bounds bounds, device;
    };
};
