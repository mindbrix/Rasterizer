//
//  Copyright 2025 Nigel Timothy Barber - nigel@mindbrix.co.uk
//
//  This software is provided 'as-is', without any express or implied
//  warranty. In no event will the authors be held liable for any damages
//  arising from the use of this software.
//
//  Permission is granted to anyone to use this software for personal use
//  (for a commercial licence please contact the author), and to alter it and
//  redistribute it freely, subject to the following restrictions:
//
//  1. The origin of this software must not be misrepresented; you must not
//  claim that you wrote the original software. If you use this software
//  in a product, an acknowledgment in the product documentation would be
//  appreciated but is not required.
//  2. Altered source versions must be plainly marked as such, and must not be
//  misrepresented as being the original software.
//  3. This notice may not be removed or altered from any source distribution.
//

#import <Metal/Metal.h>
#import "Rasterizer.hpp"


struct RenderBuffer {
    void allocHeader(size_t n) {
        header = 0;
        resize(n);
        header = n;
    }
    void allocContextSlices(size_t n) {
        resize(n);
    }
    size_t size() const {
        return mtlBuffer ? mtlBuffer.length : 0;;
    }
    bool willShrink(size_t n) const {
        return header && size() > 1000000 && size() / n > 5;
    }
    void resize(size_t n) {
        if (size() < n || willShrink(n)) {
            id <MTLBuffer> newBuffer = [device newBufferWithLength:n * 4 / 3 options:MTLResourceStorageModeShared];
            
            if (header && header <= size())
                memcpy(newBuffer.contents, mtlBuffer.contents, header);
            mtlBuffer = newBuffer;
            buffer.base = (uint8_t *)mtlBuffer.contents;
        }
    }
    size_t header = 0;
    Ra::Buffer buffer;
    id<MTLDevice> device;
    id <MTLBuffer> mtlBuffer;
};


struct RasterizerRenderer {
    RasterizerRenderer() {
        size_t count = sysconf(_SC_NPROCESSORS_ONLN);
        contexts.resize(count < 1 ? 1 : count);
    }
    
    void renderList(const Ra::SceneList& list, float scale, float w, float h, RenderBuffer *renderBuffer) {
        Ra::Buffer *buffer = & renderBuffer->buffer;
        size_t contextCount = contexts.size();
        list.prepare();
        buffer->prepare(list);
        renderBuffer->allocHeader(buffer->headerSize);
        
        auto divisions = (size_t *)alloca((contextCount + 1) * sizeof(size_t));
        writeDivisions(list, contextCount, divisions);
        dispatch_apply(contextCount, DISPATCH_APPLY_AUTO, ^(size_t i) {
            contexts[i].drawList(list, scale, w, h, divisions[i], divisions[i + 1], buffer);
        });
        auto begins = (size_t *)alloca(contextCount * sizeof(size_t));
        size_t size = Ra::resizeBuffer(list, & contexts[0], contextCount, begins, *buffer);
        bool willShrink = renderBuffer->willShrink(size);
        renderBuffer->allocContextSlices(size);
        
        Ra::writeOpaques(list, & contexts[0], contextCount, begins, *buffer);
        dispatch_apply(contextCount, DISPATCH_APPLY_AUTO, ^(size_t i) {
            Ra::writeContextToBuffer(list, & contexts[0] + i, begins[i], i, contextCount, *buffer);
        });
        for (int i = 0; i < contextCount; i++)
            for (int j = 0; j < contexts[i].entries.end(); j++)
                *(buffer->entries.alloc(1)) = contexts[i].entries[j];
        size_t end = buffer->entries.end == 0 ? 0 : buffer->entries.back().end;
        assert(size >= end);
        
        auto colors = (Ra::Color *)(buffer->base + buffer->colors);
        colors[buffer->pathsCount] = buffer->params.clearColor;
        
        if (willShrink)
            reset();
    }
    
    // Split the list into contiguous draw ranges of equal prepared weight
    static void writeDivisions(const Ra::SceneList& list, size_t count, size_t *divisions) {
        size_t total = 0, pathsCount = list.pathsCount(), sum = 0, base = 0, j = 0, target;
        for (auto& scene : list.scenes)
            total += scene->weight;
        divisions[0] = 0, divisions[count] = pathsCount;
        for (size_t i = 1; i < count; i++) {
            if (total == 0) {
                divisions[i] = i * pathsCount / count;
                continue;
            }
            for (target = total * i / count; j < list.scenes.size() && sum + list.scenes[j]->weight < target; j++)
                sum += list.scenes[j]->weight, base += list.scenes[j]->count();
            if (j == list.scenes.size())
                divisions[i] = pathsCount;
            else {
                const Ra::Scene& scene = *list.scenes[j].ptr;
                divisions[i] = base + (std::lower_bound(scene.weights.base, scene.weights.base + scene.count(), target - sum) - scene.weights.base);
            }
        }
    }
    
    void reset() {
        for (auto& ctx : contexts)
            ctx.reset();
    }
    
    std::vector<Ra::Context> contexts;
 };
