//
//  RasterizerThread.hpp
//  Rasterizer
//
//  Created by Nigel Barber on 23/06/2019.
//  Copyright © 2019 @mindbrix. All rights reserved.
//

#include <pthread.h>
#define NUM_THREADS 5

struct RasterizerThread {
    typedef void (*Function)(void *info);
    
    struct Arguments {
        Arguments() {}
        Arguments(Function function, void *info) : function(function), info(info) {}
        Function function = nullptr;
        void *info = nullptr;
    };

    static void *perform_work(void *arguments) {
        Arguments *args = (Arguments *)arguments;
        (*args->function)(args->info);
        return 0;
    }
    
    static void foreach(Function function, void *arguments, size_t count, size_t stride) {
        pthread_t threads[count];
        Arguments args[count];
        int i;
        int result_code;
        
        uint8_t *base = (uint8_t *)arguments;
        for (i = 0; i < count; i++, base += stride)
            args[i] = Arguments(function, (void *)base);
            
        //create all threads one by one
        for (i = 0; i < count; i++) {
            //            printf("IN MAIN: Creating thread %d.\n", i);
            result_code = pthread_create(& threads[i], NULL, perform_work, & args[i]);
            assert(!result_code);
        }
        
        printf("IN MAIN: All threads are created.\n");
        
        //wait for each thread to complete
        for (i = 0; i < count; i++) {
            result_code = pthread_join(threads[i], NULL);
            assert(!result_code);
            //            printf("IN MAIN: Thread %d has ended.\n", i);
        }
        
        printf("MAIN program has ended.\n");
    }
};
