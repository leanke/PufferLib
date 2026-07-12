#include "terraria.h"

#define OBS_SIZE    543
#define NUM_ATNS    5
#define ACT_SIZES   {6, 4, 32, NUM_RECIPES + 1, 10}
#define OBS_TENSOR_T FloatTensor

#define Env Terraria
#include "vecenv.h"

void my_init(Env* env, Dict* kwargs) {
    (void)kwargs;
    env->num_agents = 1;
    c_init(env);
}

void my_log(Log* log, Dict* out) {
    dict_set(out, "perf",            log->perf);
    dict_set(out, "score",           log->score);
    dict_set(out, "episode_return",  log->episode_return);
    dict_set(out, "episode_length",  log->episode_length);
    dict_set(out, "tiles_mined",     log->tiles_mined);
    dict_set(out, "items_crafted",   log->items_crafted);
    dict_set(out, "enemies_killed",  log->enemies_killed);
    dict_set(out, "depth_reached",   log->depth_reached);
    dict_set(out, "hardmode_reached", log->hardmode_reached);
    dict_set(out, "n",               log->n);
}
