// TODO: Replicate correct green for grass
// TODO: Vanishing point for road should terminate in single pixel, not 2
// TODO: Collision drift doesn't work. Make it work.
// TODO: Add collisions with the road edges
// TODO: Fog mode - don't render road lines
// TODO: Reduced handling on snow
// TODO: Ascertain exact atari enemy car spawn frequency per each day
// TODO: Ascertain top spawn lane choice logic per atari original
// TODO: Implement RL in pufferlib
// TODO: Make sure it trains
// TODO: Engineer good policy
// TODO: Add enemy car tail lights for night (2 colors??)
// TODO: Ascertain original atari scoring logic and implement (differs from reward)
// TODO: Fix pile spawning when speed < 0 (enemy cars spawn on top of each other)
// TODO: Fix loss terminal logic (currently doesn't end episode)

// Leanke's TODOs:
// TODO: Combine 2 fns for leanke
// TODO: reduce line count

// takes about 14 seconds to fully accelerate from min speed to max speed
// there appear to be 4 gears with timings as follows:
// 1. 4 seconds
// 2. 2.5 seconds
// 3. 3.25 seconds
// 4. 1.5 seconds
// at max speed, enemy cars take around 0.4 seconds to travel from the
// spawn point at the top to the bottom of the screen
// at min speed, enemy cars take around 0.5 seconds to
// travel from the spawn point at the top to the bottom of the screen
// to the vanishing point at the top


// enduro_clone.c

#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include "enduro_clone.h"

int main() {
    Enduro env = {
        .width = SCREEN_WIDTH,
        .height = SCREEN_HEIGHT,
        .car_width = CAR_WIDTH,
        .car_height = CAR_HEIGHT,
        .max_enemies = MAX_ENEMIES,
        .crash_noop_duration = CRASH_NOOP_DURATION,
        .day_length = DAY_LENGTH,
        .initial_cars_to_pass = INITIAL_CARS_TO_PASS,
        .min_speed = MIN_SPEED,
        .max_speed = MAX_SPEED,
    };

    allocate(&env);
    // Initialize raylib
    initRaylib();
    // Create client
    Client* client = make_client(&env);
    // Load textures
    loadTextures(&env.gameState);
    reset(&env);
    int running = 1;

    // Main game loop
    while (running) {
        // Handle events
        handleEvents(&running, &env);
        // Step the environment
        step(&env);
        // Update game state
        updateBackground(&env.gameState); // Ensure day cycles correctly
        updateCarAnimation(&env.gameState, &env);
        updateMountains(&env.gameState, &env);
        // Update victory effects
        updateVictoryEffects(&env.gameState);
        // Update score
        updateScore(&env.gameState);
        // Render everything
        render(client, &env);
        // Check if the window should close
        if (WindowShouldClose()) {
            running = 0;
        }
    }

    // Cleanup
    cleanup(&env.gameState);
    close_client(client);
    free_allocated(&env);
    return 0;
}
