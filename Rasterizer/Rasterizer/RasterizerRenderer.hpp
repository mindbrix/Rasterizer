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
         Ra::Context *context;
         Ra::SceneList *list;  RasterizerState *state;
         uint32_t *idxs;  Ra::Transform *ctms;  Ra::Colorant *colors;  Ra::Transform *clips;  float *widths;  Ra::Bounds *bounds;
         size_t begin;  std::vector<Ra::Buffer::Entry> *entries;
         Ra::Buffer *buffer;
    };
    static void drawList(void *info) {
        ThreadInfo *ti = (ThreadInfo *)info;
        ti->context->drawList(*ti->list, ti->state->view, ti->idxs, ti->ctms, ti->colors, ti->clips, ti->widths, ti->bounds, ti->state->outlineWidth, ti->buffer);
    }
    static void writeContextsToBuffer(void *info) {
        ThreadInfo *ti = (ThreadInfo *)info;
        Ra::writeContextToBuffer(*ti->list, ti->context, ti->idxs, ti->begin, *ti->entries, *ti->buffer);
    }
    void renderList(Ra::SceneList& list, RasterizerState& state, Ra::Buffer *buffer) {
        assert(sizeof(uint32_t) == sizeof(Ra::Colorant));
        if (list.pathsCount == 0)
            return;
        ThreadInfo threadInfo, *ti = & threadInfo;
        buffer->prepare(list.pathsCount), buffer->useCurves = state.useCurves;
        ti->context = contexts, ti->list = & list, ti->state = & state, ti->buffer = buffer;
        ti->idxs = (uint32_t *)malloc(list.pathsCount * sizeof(uint32_t));
        ti->ctms = (Ra::Transform *)(buffer->base + buffer->transforms);
        ti->clips = (Ra::Transform *)(buffer->base + buffer->clips);
        ti->colors = (Ra::Colorant *)(buffer->base + buffer->colors);
        ti->widths = (float *)(buffer->base + buffer->widths),
        ti->bounds = (Ra::Bounds *)(buffer->base + buffer->bounds);
        for (size_t i = 0, iz = 0; i < list.scenes.size(); iz += list.scenes[i].count, i++)
            memcpy(ti->colors + iz, & list.scenes[i].colors[0].src0, list.scenes[i].count * sizeof(Ra::Colorant));
        renderListOnQueues(list, state, ti);
        free(ti->idxs);
    }
    
    void renderListOnQueues(Ra::SceneList& list, RasterizerState& state, ThreadInfo *info) {
        size_t total = 0, count, base, i, iz, izeds[kQueueCount + 1], target, *izs = izeds;
        for (int j = 0; j < list.scenes.size(); j++)
            total += list.scenes[j].weight;
        izeds[0] = 0, izeds[kQueueCount] = list.pathsCount;
        auto scene = & list.scenes[0];
        for (count = base = iz = 0, i = 1; i < kQueueCount; i++) {
            for (target = total * i / kQueueCount; count < target; iz++) {
                if (iz - base == scene->count)
                    scene++, base = iz;
                count += scene->paths[iz - base].ref->types.end;
            }
            izeds[i] = iz;
        }
        ThreadInfo threadInfo[kQueueCount];
        for (i = 0; i < kQueueCount; i++)
            threadInfo[i] = *info, threadInfo[i].context += i;
        for (i = 0; i < kQueueCount; i++)
            contexts[i].prepare(state.device.ux, state.device.uy, list.pathsCount, izs[i], izs[i + 1]);
        RasterizerQueue::scheduleAndWait(queues, kQueueCount, drawList, threadInfo, sizeof(ThreadInfo), kQueueCount);
        std::vector<Ra::Buffer::Entry> entries[kQueueCount];
        size_t begins[kQueueCount], size = Ra::writeContextsToBuffer(list, contexts, kQueueCount, begins, *info->buffer);
        for (i = 0; i < kQueueCount; i++)
            threadInfo[i].begin = begins[i], threadInfo[i].entries = & entries[i];
        RasterizerQueue::scheduleAndWait(queues, kQueueCount, writeContextsToBuffer, threadInfo, sizeof(ThreadInfo), kQueueCount);
        for (int i = 0; i < kQueueCount; i++)
            for (auto entry : entries[i])
                *(info->buffer->entries.alloc(1)) = entry;
        size_t end = info->buffer->entries.end == 0 ? 0 : info->buffer->entries.base[info->buffer->entries.end - 1].end;
        assert(size >= end);
    }
    
    static const int kQueueCount = 8;
    void reset() { for (auto& ctx : contexts) ctx.reset(); }
    Ra::Context contexts[kQueueCount];
    RasterizerQueue queues[kQueueCount];
 };

 typedef RasterizerRenderer RaR;
