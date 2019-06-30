//
//  RasterizerQueue.hpp
//  Rasterizer
//
//  Created by Nigel Barber on 23/06/2019.
//  Copyright © 2019 @mindbrix. All rights reserved.
//
#import <vector>
#import <pthread.h>
#pragma clang diagnostic ignored "-Wcomma"

struct RasterizerQueue {
    RasterizerQueue() {
        pthread_mutex_init(& mtx, NULL), pthread_cond_init(& notempty, NULL), pthread_cond_init(& empty, NULL);
        pthread_create(& thread, NULL, thread_main, (void *)this);
    }
    ~RasterizerQueue() {
        pthread_cond_signal(& notempty);
        pthread_join(thread, NULL);
        assert(pthread_mutex_destroy(& mtx) == 0), pthread_cond_destroy(& notempty), pthread_cond_destroy(& empty);
    }
    typedef void (*Function)(void *info);
    static void scheduleAndWait(RasterizerQueue *queues, size_t qcount, Function function, void *info, size_t infostride, size_t count) {
        if (queues == nullptr || qcount == 0 || function == nullptr || info == nullptr || infostride == 0 || count == 0)
            return;
        for (int i = 0; i < count; i++)
            queues[i % qcount].add(function, (void *)((uint8_t *)info + i * infostride));
        for (int i = 0; i < qcount; i++)
            queues[i].wait();
    }
private:
    struct Call {
        Call() {}
        Call(Function function, void *info) : function(function), info(info) {}
        Function function = nullptr;
        void *info = nullptr;
    };
    static void *thread_main(void *args) {
        return ((RasterizerQueue *)args)->loop();
    }
    void add(Function function, void *info) {
        pthread_mutex_lock(& mtx);
        if (calls.size() == 0)
            pthread_cond_signal(& notempty);
        calls.emplace_back(function, info);
        pthread_mutex_unlock(& mtx);
    }
    void *loop() {
        while (1) {
            pthread_mutex_lock(& mtx);
            if (calls.size() == 0)
                pthread_cond_wait(& notempty, & mtx);
            Call call = calls.size() ? calls[0] : Call();
            pthread_mutex_unlock(& mtx);
            if (call.function == nullptr || call.info == nullptr)
                return nullptr;
            (*call.function)(call.info);
            pthread_mutex_lock(& mtx);
            calls.erase(calls.begin());
            if (calls.size() == 0)
                pthread_cond_signal(& empty);
            pthread_mutex_unlock(& mtx);
        }
        return nullptr;
    }
    void wait() {
        pthread_mutex_lock(& mtx);
        if (calls.size())
            pthread_cond_wait(& empty, & mtx);
        pthread_mutex_unlock(& mtx);
    }
    pthread_t thread;
    pthread_mutex_t mtx;
    pthread_cond_t notempty, empty;
    std::vector<Call> calls;
};
