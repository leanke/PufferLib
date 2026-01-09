#include "mgba.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef STANDALONE_BUILD
#include "raylib.h"

#define WINDOW_SCALE 3
#define WINDOW_WIDTH (SCREEN_WIDTH * WINDOW_SCALE)
#define WINDOW_HEIGHT (SCREEN_HEIGHT * WINDOW_SCALE)

// Pixel buffer for raylib (RGBA format)
static unsigned char g_pixels[SCREEN_PIXELS * 4];

// GBA key bits: A=0, B=1, Select=2, Start=3, Right=4, Left=5, Up=6, Down=7
static uint32_t get_keyboard_input(void) {
  uint32_t keys = 0;

  if (IsKeyDown(KEY_Z) || IsKeyDown(KEY_X))
    keys |= (1 << 0); // A
  if (IsKeyDown(KEY_A) || IsKeyDown(KEY_S))
    keys |= (1 << 1); // B
  if (IsKeyDown(KEY_BACKSPACE))
    keys |= (1 << 2); // Select
  if (IsKeyDown(KEY_ENTER))
    keys |= (1 << 3); // Start
  if (IsKeyDown(KEY_RIGHT))
    keys |= (1 << 4); // Right
  if (IsKeyDown(KEY_LEFT))
    keys |= (1 << 5); // Left
  if (IsKeyDown(KEY_UP))
    keys |= (1 << 6); // Up
  if (IsKeyDown(KEY_DOWN))
    keys |= (1 << 7); // Down

  return keys;
}

static void render_frame(mGBA *env, Texture2D *tex) {
  if (!env || !env->video_buffer)
    return;

  // Convert video buffer to RGBA format for raylib
  for (int i = 0; i < SCREEN_PIXELS; i++) {
    color_t p = env->video_buffer[i];
    g_pixels[i * 4 + 0] = (p >> 16) & 0xFF; // R
    g_pixels[i * 4 + 1] = (p >> 8) & 0xFF;  // G
    g_pixels[i * 4 + 2] = p & 0xFF;         // B
    g_pixels[i * 4 + 3] = 255;              // A
  }

  UpdateTexture(*tex, g_pixels);

  BeginDrawing();
  ClearBackground(BLACK);
  DrawTextureEx(*tex, (Vector2){0, 0}, 0.0f, WINDOW_SCALE, WHITE);

  // Draw HUD info
  DrawText(TextFormat("Score: %.2f", env->score), 10, WINDOW_HEIGHT - 60, 16,
           GREEN);
  DrawText(TextFormat("X:%d Y:%d Map:%d", env->x, env->y, env->map_n), 10,
           WINDOW_HEIGHT - 40, 16, GREEN);
  DrawText(TextFormat("Step: %d", env->step_count), 10, WINDOW_HEIGHT - 20, 16,
           GREEN);

  EndDrawing();
}

int main(int argc, char **argv) {
  if (argc < 2) {
    fprintf(stderr, "Usage: %s <rom_path>\n", argv[0]);
    fprintf(stderr, "\nControls:\n");
    fprintf(stderr, "  Arrow keys: D-pad\n");
    fprintf(stderr, "  Z/X: A button\n");
    fprintf(stderr, "  A/S: B button\n");
    fprintf(stderr, "  Enter: Start\n");
    fprintf(stderr, "  Backspace: Select\n");
    fprintf(stderr, "  F1: Save state\n");
    fprintf(stderr, "  F2: Load state\n");
    fprintf(stderr, "  R: Reset\n");
    fprintf(stderr, "  ESC: Quit\n");
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
  env.frame_skip = 1;              // Lower frame skip for better control
  env.max_episode_length = 999999; // Long episode for manual play

  printf("Initializing mGBA core...\n");
  mgba_init_core(&env, argv[1]);

  if (!env.core) {
    fprintf(stderr, "Failed to initialize core\n");
    return 1;
  }
  printf("mGBA core initialized successfully\n");
  printf("Video buffer: %p\n", (void *)env.video_buffer);

  // Initialize raylib window
  printf("Initializing raylib...\n");
  SetTraceLogLevel(LOG_WARNING);
  InitWindow(WINDOW_WIDTH, WINDOW_HEIGHT, "mGBA - Pokemon Red");
  printf("Window created\n");
  SetTargetFPS(60);

  // Create texture for rendering (RGBA format =
  // PIXELFORMAT_UNCOMPRESSED_R8G8B8A8)
  printf("Creating texture...\n");
  Image img = {.data = g_pixels,
               .width = SCREEN_WIDTH,
               .height = SCREEN_HEIGHT,
               .mipmaps = 1,
               .format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8};
  Texture2D tex = LoadTextureFromImage(img);
  printf("Texture created: id=%d\n", tex.id);

  printf("Calling c_reset...\n");
  c_reset(&env);
  printf("Game started! Use arrow keys to move, Z/X for A, A/S for B\n");
  printf("Press F1 to save, F2 to load, R to reset, ESC to quit\n");

  while (!WindowShouldClose()) {
    // Handle special keys
    if (IsKeyPressed(KEY_F1)) {
      if (c_save_state_file(&env, "manual_save.state")) {
        printf("State saved!\n");
      }
    }
    if (IsKeyPressed(KEY_F2)) {
      if (c_load_state_file(&env, "manual_save.state")) {
        printf("State loaded!\n");
      }
    }
    if (IsKeyPressed(KEY_R)) {
      c_reset(&env);
      printf("Reset!\n");
    }

    // Get keyboard input and step
    action = get_keyboard_input();
    c_step(&env);

    // Render
    render_frame(&env, &tex);

    if (terminal || truncation) {
      printf("Episode finished! Score: %.2f\n", env.score);
      c_reset(&env);
    }
  }

  UnloadTexture(tex);
  CloseWindow();

  c_close(&env);
  printf("Done!\n");
  return 0;
}

#else
// Non-standalone build (for testing without raylib)
int main(int argc, char **argv) {
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
#endif
