//
//  RasterizerState.hpp
//  Rasterizer
//
//  Created by Nigel Barber on 20/03/2019.
//  Copyright © 2019 @mindbrix. All rights reserved.
//
#import "Rasterizer.hpp"
#import "RasterizerWinding.hpp"

struct RasterizerState {
    struct Event {
        enum Flags { kCapsLock = 1 << 16, kShift = 1 << 17, kControl = 1 << 18, kOption = 1 << 19, kCommand = 1 << 20, kNumericPad = 1 << 21, kHelp = 1 << 22, kFunction = 1 << 23 };
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
    void update(float s, float w, float h) {
        scale = s, view = Ra::Transform(s, 0.f, 0.f, s, 0.f, 0.f).concat(ctm);
        bounds = Ra::Bounds(0.f, 0.f, w, h), device = Ra::Bounds(0.f, 0.f, ceilf(s * w), ceilf(h * s));
    }
    bool writeEvent(Event e) {
        bool written = e.type != Event::kKeyDown || (e.type == Event::kKeyDown && (e.keyCode == 8 || e.keyCode == 31 || e.keyCode == 35 || e.keyCode == 36));
        if (written)
            events.emplace_back(e);
        return written;
    }
    bool readEvents(double time) {
        bool redraw = false;
        for (Event& e : events) {
            switch(e.type) {
                case Event::kMouseMove:
                    x = e.x, y = e.y;
                    redraw = mouseMove;
                    break;
                case Event::kMouseUp:
                    mouseDown = false;
                    break;
                case Event::kMouseDown:
                    mouseDown = true;
                    break;
                case Event::kFlags:
                    flags = e.flags;
                    break;
                case Event::kKeyDown:
                    keyDown = true, keyCode = e.keyCode;
                    redraw = true;
                    if (e.keyCode == 8)
                        useCurves = !useCurves;
                    else if (e.keyCode == 31)
                        outlineWidth = outlineWidth ? 0.f : -1.f;
                    else if (e.keyCode == 35)
                        mouseMove = !mouseMove;
                    else if (e.keyCode == 36) {
                        if (user.lx == FLT_MAX)
                            ctm = { 1.f, 0.f, 0.f, 1.f, 0.f, 0.f };
                        else {
                            float sx = (bounds.ux - bounds.lx) / (user.ux - user.lx), sy = (bounds.uy - bounds.ly) / (user.uy - user.ly), scale = sx < sy ? sx : sy;
                            Ra::Transform fit = { scale, 0.f, 0.f, scale, -scale * user.lx, -scale * user.ly };
                            if (ctm.a == fit.a && ctm.b == fit.b && ctm.c == fit.c && ctm.d == fit.d && ctm.tx == fit.tx && ctm.ty == fit.ty)
                                ctm = { 1.f, 0.f, 0.f, 1.f, 0.f, 0.f };
                            else
                                ctm = fit;
                        }
                    }
                    break;
                case Event::kKeyUp:
                    keyDown = true, keyCode = e.keyCode;
                    break;
                case Event::kMagnify:
                    ctm = ctm.concat(Ra::Transform(e.x, 0.f, 0.f, e.x, 0.f, 0.f), 0.5f * (bounds.lx + bounds.ux), 0.5f * (bounds.ly + bounds.uy));
                    redraw = true;
                    break;
                case Event::kRotate:
                    ctm = ctm.concat(Ra::Transform::rotation(e.x), 0.5f * (bounds.lx + bounds.ux), 0.5f * (bounds.ly + bounds.uy));
                    redraw = true;
                    break;
                case Event::kTranslate:
                    ctm.tx += e.x, ctm.ty += e.y;
                    redraw = true;
                    break;
                case Event::kNull:
                    assert(0);
            }
        }
        view = Ra::Transform(scale, 0.f, 0.f, scale, 0.f, 0.f).concat(ctm);
        events.resize(0);
        return redraw;
    }
    void doMouseMove(Ra::SceneList& list) {
        index = INT_MAX;
        if (mouseMove) {
            Ra::SceneList visibles;
            list.writeVisibles(view, device, visibles);
            if (visibles.pathsCount) {
                Ra::Range indices = RasterizerWinding::indicesForPoint(visibles, view, device, scale * x, scale * y);
                if (indices.begin != INT_MAX) {
                    int idx = 0;
                    for (int j = 0; j < indices.begin; j++)
                        idx += visibles.scenes[j].count;
                        index = idx + indices.end;
                }
            }
        }
    }
    bool keyDown = false, mouseDown = false, mouseMove = false, useCurves = true;
    float x, y, scale, outlineWidth = 0.f;
    int keyCode = 0;
    size_t index = INT_MAX, flags = 0;
    std::vector<Event> events;
    Ra::Transform ctm, view;
    Ra::Bounds bounds, device, user;
};
