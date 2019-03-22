//
//  RasterizerEvent.hpp
//  Rasterizer
//
//  Created by Nigel Barber on 20/03/2019.
//  Copyright © 2019 @mindbrix. All rights reserved.
//
#import <vector>

struct RasterizerEvent {
    struct Event {
        enum Type { kNull = 0, kMouseMove, kMouseUp, kMouseDown, kKeyDown, kKeyUp, kMagnify, kRotate, kTranslate };
        
        Event() {}
        Event(double time, Type type, float x, float y) : time(time), type(type), x(x), y(y), keyCode(0) {}
        Event(double time, Type type, int keyCode) : time(time), type(type), x(0.f), y(0.f), keyCode(keyCode) {}
        
        double time;
        Type type;
        float x, y;
        int keyCode;
    };
    
    struct State {
        bool keyDown = false, mouseDown = false;
        int keyCode = 0;
        std::vector<RasterizerEvent::Event> events;
    };
};
