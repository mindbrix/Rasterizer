//
//  RasterizerRenderer.hpp
//  Rasterizer
//
//  Created by Nigel Barber on 10/05/2020.
//  Copyright © 2020 @mindbrix. All rights reserved.
//

#import "RasterizerQueue.hpp"
#import "RasterizerState.hpp"

struct RasterizerRenderer {
     struct ThreadInfo {
         Ra::Context *context;  Ra::SceneList *list;  Ra::Transform view;
         Ra::Buffer *buffer;  size_t begin;
    };
    static void drawList(void *info) {
        ThreadInfo *ti = (ThreadInfo *)info;
        ti->context->drawList(*ti->list, ti->view, ti->buffer);
    }
    static void writeContextsToBuffer(void *info) {
        ThreadInfo *ti = (ThreadInfo *)info;
        Ra::writeContextToBuffer(*ti->list, ti->context, ti->begin, *ti->buffer);
    }
    void renderList(Ra::SceneList& list, RasterizerState& state, Ra::Buffer *buffer) {
        assert(sizeof(uint32_t) == sizeof(Ra::Colorant));
        if (list.pathsCount == 0)
            return;
        buffer->prepare(list), buffer->useCurves = state.useCurves, buffer->fastOutlines = state.fastOutlines;
        size_t i, izs[kQueueCount + 1];  writeIzs(list, izs);
        ThreadInfo threadInfo[kQueueCount], *ti;
        for (ti = threadInfo, i = 0; i < kQueueCount; i++, ti++) {
            ti->list = & list, ti->view = state.view, ti->buffer = buffer;
            ti->context = contexts + i, ti->context->prepare(state.device, list.pathsCount, izs[i], izs[i + 1]);
        }
        RasterizerQueue::scheduleAndWait(queues, kQueueCount, drawList, threadInfo, sizeof(ThreadInfo), kQueueCount);
        size_t begins[kQueueCount], size = Ra::writeContextsToBuffer(list, contexts, kQueueCount, begins, *buffer);
        for (ti = threadInfo, i = 0; i < kQueueCount; i++, ti++)
            ti->begin = begins[i];
        RasterizerQueue::scheduleAndWait(queues, kQueueCount, writeContextsToBuffer, threadInfo, sizeof(ThreadInfo), kQueueCount);
        for (int i = 0; i < kQueueCount; i++)
            for (auto entry : contexts[i].entries)
                *(buffer->entries.alloc(1)) = entry;
        size_t end = buffer->entries.end == 0 ? 0 : buffer->entries.back().end;
        assert(size >= end);
    }
    
    void writeIzs(Ra::SceneList& list, size_t *izs) {
        size_t total = 0, count, base, i, iz, target;
        for (int j = 0; j < list.scenes.size(); j++)
            total += list.scenes[j].weight;
        izs[0] = 0, izs[kQueueCount] = list.pathsCount;
        auto scene = & list.scenes[0];
        for (count = base = iz = 0, i = 1; i < kQueueCount; i++) {
            for (target = total * i / kQueueCount; count < target; iz++) {
                if (iz - base == scene->count)
                    scene++, base = iz;
                count += scene->cache->entryAt(iz - base)->g->types.end;
            }
            izs[i] = iz;
        }
    }
    static const int kQueueCount = 8;
    void reset() { for (auto& ctx : contexts) ctx.reset(); }
    Ra::Context contexts[kQueueCount];
    RasterizerQueue queues[kQueueCount];
 };


 typedef RasterizerRenderer RaR;
