// Port of PufferLib's Cython multi-snake env to C. There is no performance
// benefit to this implementation. It is purely so we can compile to WASM
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <math.h>
#include "raylib.h"

#define EMPTY 0
#define FOOD 1
#define CORPSE 2
#define WALL 3

#define LOG_BUFFER_SIZE 8192

typedef struct Log Log;
struct Log {
    float episode_return;
    float episode_length;
    float score;
};

typedef struct LogBuffer LogBuffer;
struct LogBuffer {
    Log* logs;
    int length;
    int idx;
};

LogBuffer* allocate_logbuffer(int size) {
    LogBuffer* logs = (LogBuffer*)calloc(1, sizeof(LogBuffer));
    logs->logs = (Log*)calloc(size, sizeof(Log));
    logs->length = size;
    logs->idx = 0;
    return logs;
}

void free_logbuffer(LogBuffer* buffer) {
    free(buffer->logs);
    free(buffer);
}

void add_log(LogBuffer* logs, Log* log) {
    if (logs->idx == logs->length) {
        return;
    }
    logs->logs[logs->idx] = *log;
    logs->idx += 1;
    //printf("Log: %f, %f, %f\n", log->episode_return, log->episode_length, log->score);
}

Log aggregate_and_clear(LogBuffer* logs) {
    Log log = {0};
    if (logs->idx == 0) {
        return log;
    }
    for (int i = 0; i < logs->idx; i++) {
        log.episode_return += logs->logs[i].episode_return;
        log.episode_length += logs->logs[i].episode_length;
        log.score += logs->logs[i].score;
    }
    log.episode_return /= logs->idx;
    log.episode_length /= logs->idx;
    log.score /= logs->idx;
    logs->idx = 0;
    return log;
}
 
typedef struct {
    char* observations;
    unsigned int* actions;
    float* rewards;
    unsigned char* terminals;
    LogBuffer* log_buffer;
    Log* logs;
    char* grid;
    int* snake;
    int* snake_lengths;
    int* snake_ptr;
    int* snake_lifetimes;
    int* snake_colors;
    int num_snakes;
    int width;
    int height;
    int max_snake_length;
    int food;
    int vision;
    int window;
    int obs_size;
    unsigned char leave_corpse_on_death;
    float reward_food;
    float reward_corpse;
    float reward_death;
} CSnake;

void init_csnake(CSnake* env) {
    env->grid = (char*)calloc(env->width*env->height, sizeof(char));
    env->snake = (int*)calloc(env->num_snakes*2*env->max_snake_length, sizeof(int));
    env->snake_lengths = (int*)calloc(env->num_snakes, sizeof(int));
    env->snake_ptr = (int*)calloc(env->num_snakes, sizeof(int));
    env->snake_lifetimes = (int*)calloc(env->num_snakes, sizeof(int));
    env->snake_colors = (int*)calloc(env->num_snakes, sizeof(int));
    env->logs = calloc(env->num_snakes, sizeof(Log));
    env->snake_colors[0] = 9;
    for (int i = 1; i<env->num_snakes; i++)
        env->snake_colors[i] = i%4 + 4; // Randomize snake colors
}
 
void allocate_csnake(CSnake* env) {
    int obs_size = (2*env->vision + 1) * (2*env->vision + 1);
    env->observations = (char*)calloc(env->num_snakes*obs_size, sizeof(char));
    env->actions = (unsigned int*)calloc(env->num_snakes, sizeof(unsigned int));
    env->rewards = (float*)calloc(env->num_snakes, sizeof(float));
    env->log_buffer = allocate_logbuffer(LOG_BUFFER_SIZE);
    init_csnake(env);
}

void free_csnake(CSnake* env) {
    if (env == NULL)
        return;

    free(env->grid);
    free(env->observations);
    free(env->snake);
    free(env->snake_lengths);
    free(env->snake_ptr);
    free(env->snake_lifetimes);
    free(env->snake_colors);
    free(env->actions);
    free(env->rewards);
    free_logbuffer(env->log_buffer);
    free(env->logs);
    free(env);
}

void compute_observations(CSnake* env) {
    for (int i = 0; i < env->num_snakes; i++) {
        int head_ptr = i*2*env->max_snake_length + 2*env->snake_ptr[i];
        int r_offset = env->snake[head_ptr] - env->vision;
        int c_offset = env->snake[head_ptr+1] - env->vision;
        for (int r = 0; r < 2 * env->vision + 1; r++) {
            for (int c = 0; c < 2 * env->vision + 1; c++) {
                env->observations[i*env->obs_size + r*env->window + c] = env->grid[
                    (r_offset + r)*env->width + c_offset + c];
            }
        }
    }
}

void delete_snake(CSnake* env, int snake_id) {
    while (env->snake_lengths[snake_id] > 0) {
        int head_ptr = env->snake_ptr[snake_id];
        int head_offset = 2*env->max_snake_length*snake_id + 2*head_ptr;
        int head_r = env->snake[head_offset];
        int head_c = env->snake[head_offset + 1];
        if (env->leave_corpse_on_death && env->snake_lengths[snake_id] % 2 == 0)
            env->grid[head_r*env->width + head_c] = CORPSE;
        else
            env->grid[head_r*env->width + head_c] = EMPTY;

        env->snake[head_offset] = -1;
        env->snake[head_offset + 1] = -1;
        env->snake_lengths[snake_id]--;
        if (head_ptr == 0)
            env->snake_ptr[snake_id] = env->max_snake_length - 1;
        else
            env->snake_ptr[snake_id]--;
    }
}

void spawn_snake(CSnake* env, int snake_id) {
    env->logs[snake_id] = (Log){0};
    int head_r, head_c, tile, grid_idx;
    delete_snake(env, snake_id);
    do {
        head_r = rand() % (env->height - 1);
        head_c = rand() % (env->width - 1);
        grid_idx = head_r*env->width + head_c;
        tile = env->grid[grid_idx];
    } while (tile != EMPTY && tile != CORPSE);
    int snake_offset = 2*env->max_snake_length*snake_id;
    env->snake[snake_offset] = head_r;
    env->snake[snake_offset + 1] = head_c;
    env->snake_lengths[snake_id] = 1;
    env->snake_ptr[snake_id] = 0;
    env->snake_lifetimes[snake_id] = 0;
    env->grid[grid_idx] = env->snake_colors[snake_id];
}

void spawn_food(CSnake* env) {
    int idx, tile;
    do {
        int r = rand() % (env->height - 1);
        int c = rand() % (env->width - 1);
        idx = r*env->width + c;
        tile = env->grid[idx];
    } while (tile != EMPTY && tile != CORPSE);
    env->grid[idx] = FOOD;
}

void reset(CSnake* env) {
    env->window = 2*env->vision+1;
    env->obs_size = env->window*env->window;

    for (int r = 0; r < env->vision; r++) {
        for (int c = 0; c < env->width; c++)
            env->grid[r*env->width + c] = WALL;
    }
    for (int r = env->height - env->vision; r < env->height; r++) {
        for (int c = 0; c < env->width; c++)
            env->grid[r*env->width + c] = WALL;
    }
    for (int r = 0; r < env->height; r++) {
        for (int c = 0; c < env->vision; c++)
            env->grid[r*env->width + c] = WALL;
        for (int c = env->width - env->vision; c < env->width; c++)
            env->grid[r*env->width + c] = WALL;
    }
    for (int i = 0; i < env->num_snakes; i++)
        spawn_snake(env, i);
    for (int i = 0; i < env->food; i++)
        spawn_food(env);

    compute_observations(env);
}

void step_snake(CSnake* env, int i) {
    env->logs[i].episode_length += 1;
    int atn = env->actions[i];
    int dr = 0;
    int dc = 0;
    switch (atn) {
        case 0: dr = -1; break; // up
        case 1: dr = 1; break;  // down
        case 2: dc = -1; break; // left
        case 3: dc = 1; break;  // right
    }

    int head_ptr = env->snake_ptr[i];
    int snake_offset = 2*env->max_snake_length*i;
    int head_offset = snake_offset + 2*head_ptr;
    int next_r = dr + env->snake[head_offset];
    int next_c = dc + env->snake[head_offset + 1];

    // Disallow moving into own neck
    int prev_head_offset = head_offset - 2;
    if (prev_head_offset < 0)
        prev_head_offset += 2*env->max_snake_length;
    int prev_r = env->snake[prev_head_offset];
    int prev_c = env->snake[prev_head_offset + 1];
    if (prev_r == next_r && prev_c == next_c) {
        next_r = env->snake[head_offset] - dr;
        next_c = env->snake[head_offset + 1] - dc;
    }

    int tile = env->grid[next_r*env->width + next_c];
    if (tile >= WALL) {
        env->rewards[i] = env->reward_death;
        env->logs[i].episode_return += env->reward_death;
        env->logs[i].score = env->snake_lengths[i];
        add_log(env->log_buffer, &env->logs[i]);
        spawn_snake(env, i);
        return;
    }

    head_ptr++; // Circular buffer
    if (head_ptr >= env->max_snake_length)
        head_ptr = 0;
    head_offset = snake_offset + 2*head_ptr;
    env->snake[head_offset] = next_r;
    env->snake[head_offset + 1] = next_c;
    env->snake_ptr[i] = head_ptr;
    env->snake_lifetimes[i]++;

    bool grow;
    if (tile == FOOD) {
        env->rewards[i] = env->reward_food;
        env->logs[i].episode_return += env->reward_food;
        spawn_food(env);
        grow = true;
    } else if (tile == CORPSE) {
        env->rewards[i] = env->reward_corpse;
        env->logs[i].episode_return += env->reward_corpse;
        grow = true;
    } else {
        env->rewards[i] = 0.0;
        grow = false;
    }

    int snake_length = env->snake_lengths[i];
    if (grow && snake_length < env->max_snake_length - 1) {
        env->snake_lengths[i]++;
    } else {
        int tail_ptr = head_ptr - snake_length;
        if (tail_ptr < 0) // Circular buffer
            tail_ptr = env->max_snake_length + tail_ptr;
        int tail_r = env->snake[snake_offset + 2*tail_ptr];
        int tail_c = env->snake[snake_offset + 2*tail_ptr + 1];
        int tail_offset = 2*env->max_snake_length*i + 2*tail_ptr;
        env->snake[tail_offset] = -1;
        env->snake[tail_offset + 1] = -1;
        env->grid[tail_r*env->width + tail_c] = EMPTY;
    }
    env->grid[next_r*env->width + next_c] = env->snake_colors[i];
}

void step(CSnake* env){
    for (int i = 0; i < env->num_snakes; i++)
        step_snake(env, i);

    compute_observations(env);
}

// Raylib client
Color COLORS[] = {
    (Color){6, 24, 24, 255},
    (Color){0, 0, 255, 255},
    (Color){0, 128, 255, 255},
    (Color){128, 128, 128, 255},
    (Color){255, 0, 0, 255},
    (Color){255, 255, 255, 255},
    (Color){255, 85, 85, 255},
    (Color){170, 170, 170, 255},
    (Color){0, 255, 255, 255},
    (Color){255, 255, 0, 255},
};

typedef struct {
    int cell_size;
    int width;
    int height;
} Client;

Client* make_client(int cell_size, int width, int height) {
    Client* client= (Client*)malloc(sizeof(Client));
    client->cell_size = cell_size;
    client->width = width;
    client->height = height;
    InitWindow(width*cell_size, height*cell_size, "Snake Game");
    SetTargetFPS(10);
    return client;
}

void close_client(Client* client) {
    CloseWindow();
    free(client);
}

void render(Client* client, CSnake* env) {
    BeginDrawing();
    ClearBackground(COLORS[0]);
    int sz = client->cell_size;
    for (int y = 0; y < env->height; y++) {
        for (int x = 0; x < env->width; x++){
            int tile = env->grid[y*env->width + x];
            if (tile != EMPTY)
                DrawRectangle(x*sz, y*sz, sz, sz, COLORS[tile]);
        }
    }
    DrawText("Reinforcement learned snakes running in your browswer! Hold W/A/S/D to control the yellow snake.", 10, 10, 20, COLORS[8]);
    DrawText("<500 lines of pure C by @jsuarez5341. Star it on GitHub/pufferai/pufferlib to support my work!", 10, 40, 20, COLORS[8]);
    EndDrawing();
}

// Simple MLP inference. Operation order matches PyTorch
void linear_layer(float* input, float* weights, float* bias, float* output,
        int batch_size, int input_dim, int output_dim) {
    for (int b = 0; b < batch_size; b++) {
        for (int o = 0; o < output_dim; o++) {
            float sum = 0.0f;
            for (int i = 0; i < input_dim; i++)
                sum += input[b*input_dim + i] * weights[o*input_dim + i];
            output[b*output_dim + o] = sum + bias[o];
        }
    }
}

void relu(float* data, int size) {
    for (int i = 0; i < size; i++)
        data[i] = fmaxf(0.0f, data[i]);
}

typedef struct {
    int batch_size;
    int input_dim;
    int hidden_dim;
    int output_dim;
    int total_weights;
    float* w1;
    float* b1;
    float* w2;
    float* b2;
    float* observations;
    float* hidden;
    float* output;
} MLP;

MLP* allocate_mlp(int batch_size, int input_dim, int hidden_dim, int output_dim) {
    MLP* mlp = (MLP*)malloc(sizeof(MLP));
    mlp->batch_size = batch_size;
    mlp->input_dim = input_dim;
    mlp->hidden_dim = hidden_dim;
    mlp->output_dim = output_dim;
    mlp->total_weights = input_dim*(hidden_dim + 1) + hidden_dim*(output_dim + 1);
    mlp->observations = (float*)malloc(batch_size * input_dim * sizeof(float));
    mlp->hidden = (float*)malloc(batch_size * hidden_dim * sizeof(float));
    mlp->output = (float*)malloc(batch_size * output_dim * sizeof(float));
    mlp->w1 = (float*)malloc(mlp->total_weights * sizeof(float));
    mlp->b1 = mlp->w1 + input_dim*hidden_dim;
    mlp->w2 = mlp->b1 + hidden_dim;
    mlp->b2 = mlp->w2 + hidden_dim*output_dim;
    return mlp;
}

void free_mlp(MLP* mlp) {
    free(mlp->observations);
    free(mlp->hidden);
    free(mlp->output);
    free(mlp->w1); // The other pointers are just offsets
    free(mlp);
}

void mlp_forward(MLP* mlp, unsigned int* actions) {
    linear_layer(mlp->observations, mlp->w1, mlp->b1, mlp->hidden,
        mlp->batch_size, mlp->input_dim, mlp->hidden_dim);
    relu(mlp->hidden, mlp->batch_size*mlp->hidden_dim);
    linear_layer(mlp->hidden, mlp->w2, mlp->b2, mlp->output,
        mlp->batch_size, mlp->hidden_dim, mlp->output_dim);
    for (int i = 0; i < mlp->batch_size; i++) {
        float max_logit = mlp->output[i*mlp->output_dim];
        for (int j = 1; j < mlp->output_dim; j++) {
            float out = mlp->output[i*mlp->output_dim + j];
            if (out > max_logit) {
                max_logit = out;
                actions[i] = j;
            }
        }
    }
}

// File format is obained by flattening and concatenating all pytorch layers
int load_weights(const char* filename, MLP* mlp) {
    FILE* file = fopen(filename, "rb");
    if (!file) {
        perror("Error opening file");
        return 1;
    }
    fseek(file, 0, SEEK_END);
    rewind(file);
    size_t read_size = fread(mlp->w1, sizeof(float), mlp->total_weights, file);
    fclose(file);
    if (read_size != mlp->total_weights) {
        perror("Error reading file");
        return 1;
    }
    return 0;
}
