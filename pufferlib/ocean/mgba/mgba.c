#include "mgba.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


int main(int argc, char** argv) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <rom_path>\n", argv[0]);
        return 1;
    }
    
    mGBA env = {0};
    float obs[TOTAL_OBSERVATIONS];
    int32_t action = 0;
    float reward = 0;
    uint8_t terminal = 0;
    uint8_t truncation = 0;
    
    env.observations = obs;
    env.actions = &action;
    env.rewards = &reward;
    env.terminals = &terminal;
    env.truncations = &truncation;
    env.frame_skip = 4;
    env.max_episode_length = 10000;
    
    mgba_init_core(&env, argv[1]);
    
    if (!env.core) {
        fprintf(stderr, "Failed to initialize core\n");
        return 1;
    }
    
    c_reset(&env);
    
    printf("Running 1000 steps...\n");
    for (int i = 0; i < 1000; i++) {
        action = rand() % 256;
        c_step(&env);
        
        if (i % 100 == 0) {
            printf("Step %d: reward=%.2f, score=%.2f\n", i, reward, env.score);
        }
        
        if (terminal || truncation) {
            printf("Episode finished at step %d!\n", i);
            c_reset(&env);
        }
    }
    
    c_close(&env);
    printf("Done!\n");
    return 0;
}
