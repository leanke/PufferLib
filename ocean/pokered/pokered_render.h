#ifndef POKERED_RENDER_H
#define POKERED_RENDER_H

void puf_render(Env* env) {
    if (!IsWindowReady()) {
        SetTraceLogLevel(LOG_WARNING);
        InitWindow(SCREEN_WIDTH * 4, PK_FRAME_H * 4, "PufferLib Pokemon Red");
        SetTargetFPS(60);
        Image img = GenImageColor(PK_FRAME_W, PK_FRAME_H, BLACK);
        ImageFormat(&img, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
        env->render_texture = LoadTextureFromImage(img);
        UnloadImage(img);
    }
    if (IsKeyDown(KEY_ESCAPE)) {
        exit(0);
    }

    if (IsKeyPressed(KEY_Z) || IsKeyPressedRepeat(KEY_Z)) {
        env->agents[0].actions[0] = PKRED_ACTION_A;
    } else if (IsKeyPressed(KEY_X) || IsKeyPressedRepeat(KEY_X)) {
        env->agents[0].actions[0] = PKRED_ACTION_B;
    } else if (IsKeyDown(KEY_RIGHT)) {
        env->agents[0].actions[0] = PKRED_ACTION_RIGHT;
    } else if (IsKeyDown(KEY_LEFT)) {
        env->agents[0].actions[0] = PKRED_ACTION_LEFT;
    } else if (IsKeyDown(KEY_UP)) {
        env->agents[0].actions[0] = PKRED_ACTION_UP;
    } else if (IsKeyDown(KEY_DOWN)) {
        env->agents[0].actions[0] = PKRED_ACTION_DOWN;
    } else {
        env->agents[0].actions[0] = -1;
    }

    if (IsKeyPressed(KEY_S)) {
        const char* save_path = "ocean/pokered/states/quicksave.state";
        const char* pk_path = "ocean/pokered/states/quicksave.pkstate";
        bool saved = false;
        if (env->be->quicksave(env->impl, save_path)) {
            printf("pokered: state saved to %s\n", save_path);
            saved = true;
        }
        PkState pk;
        if (env->be->export_state(env->impl, &pk) &&
            pk_state_write(pk_path, &pk)) {
            printf("pokered: state saved to %s\n", pk_path);
            saved = true;
        }
        if (!saved)
            fprintf(stderr, "pokered: backend '%s' cannot save its state\n", env->be->name);
    }

    if (IsKeyPressed(KEY_O)) {
        env->show_obs_view = !env->show_obs_view;
    }

    if (!env->render_pixels) {
        env->render_pixels = (uint8_t*)calloc(PK_FRAME_W * PK_FRAME_H * 4, 1);
    }

    if (env->show_obs_view && env->agents[0].observations) {
        const obs_t* obs = env->agents[0].observations;
        for (int sy = 0; sy < SCALED_HEIGHT; sy++) {
            for (int sx = 0; sx < SCALED_WIDTH; sx++) {
                uint8_t gray = (uint8_t)obs[sy * SCALED_WIDTH + sx];
                for (int dy = 0; dy < 2; dy++) {
                    for (int dx = 0; dx < 2; dx++) {
                        int x = sx * 2 + dx;
                        int y = sy * 2 + dy;
                        uint8_t* out = &env->render_pixels[(y * PK_FRAME_W + x) * 4];
                        out[0] = gray;
                        out[1] = gray;
                        out[2] = gray;
                        out[3] = 255;
                    }
                }
            }
        }

        for (int by = 0; by < BLOCKS_TALL; by++) {
            for (int bx = 0; bx < BLOCKS_WIDE; bx++) {
                bool is_player = bx == PLAYER_BLOCK_COL && by == PLAYER_BLOCK_ROW;
                bool visited = !is_player &&
                    obs[VISITED_MASK_OFFSET + by * BLOCKS_WIDE + bx] != 0.0f;
                if (!visited && !is_player)
                    continue;
                uint8_t tint_r = is_player ? 220 : 0;
                uint8_t tint_g = is_player ? 40 : 200;
                for (int py = 0; py < BLOCK_PIXELS; py++) {
                    for (int px = 0; px < BLOCK_PIXELS; px++) {
                        int x = bx * BLOCK_PIXELS + px;
                        int y = by * BLOCK_PIXELS + py;
                        uint8_t* out = &env->render_pixels[(y * PK_FRAME_W + x) * 4];
                        out[0] = (uint8_t)((out[0] + tint_r) / 2);
                        out[1] = (uint8_t)((out[1] + tint_g) / 2);
                        out[2] = (uint8_t)(out[2] / 2);
                    }
                }
            }
        }
        UpdateTexture(env->render_texture, env->render_pixels);
    } else if (env->be->frame_rgba(env->impl, env->render_pixels)) {
        UpdateTexture(env->render_texture, env->render_pixels);
    }

    BeginDrawing();
    ClearBackground(BLACK);
    DrawTexturePro(env->render_texture,
        (Rectangle){0, 0, (float)PK_FRAME_W, (float)PK_FRAME_H},
        (Rectangle){0, 0, (float)GetScreenWidth(), (float)GetScreenHeight()},
        (Vector2){0, 0}, 0.0f, WHITE);
    EndDrawing();
    puf_web_vsync();
}

#endif
