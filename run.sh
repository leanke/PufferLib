#!/usr/bin/env bash

cd "$(dirname "$0")" || exit 1
source scripts/puffer_env.sh

prompt_env() {
    local answer
    read -r -p "Env to build [e.g. breakout]: " answer
    echo "$answer"
}

prompt_bool() {
    local label="$1" default="$2" answer hint="y/N"
    [ "$default" = "True" ] && hint="Y/n"
    read -r -p "$label [$hint]: " answer
    case "$answer" in
        y|Y|yes|Yes) echo "True" ;;
        n|N|no|No) echo "False" ;;
        *) echo "$default" ;;
    esac
}

prompt_backend() {
    local answer
    read -r -p "env.backend [emulator/native, blank = emulator]: " answer >&2
    case "$answer" in
        n|N|native) echo "native" ;;
        *) echo "emulator" ;;
    esac
}

prompt_tier() {
    local answer
    read -r -p "suite tier [quick/full/all, blank = quick]: " answer >&2
    case "$answer" in
        full|all) echo "$answer" ;;
        *) echo "quick" ;;
    esac
}

prompt_total_agents() {
    local answer
    read -r -p "vec.total_agents [leave blank for default]: " answer
    echo "$answer"
}

run() {
    local mode="$1"
    shift
    if [ -z "$mode" ]; then
        exec ./menu.sh
    fi
    case "$mode" in
        train)
            local total_agents
            total_agents="$(prompt_total_agents)"
            if [ -n "$total_agents" ]; then
                set -- "$@" "--vec.total_agents=$total_agents"
            fi
            exec ./puffer train "$@"
            ;;
        eval)
            # Headless single-agent eval; run_eval already forces verbose
            # dashboard output for eval/match modes. max_episode_length=0 disables the
            # step cap (endless episode); a trailing override still wins.
            local verbose backend
            backend="$(prompt_backend)"
            verbose="$(prompt_bool "env.verbose" "True")"
            exec ./puffer eval latest "--env.backend=$backend" "--env.verbose=$verbose" "--env.max_episode_length=0" "$@"
            ;;
        pokered)
            local verbose
            ./build.sh pokered --cpu || exit 1
            verbose="$(prompt_bool "env.verbose" "True")"
            exec ./pokered "--env.verbose=$verbose" "--env.max_episode_length=0" "$@"
            ;;
        build)
            local env
            if [ $# -gt 0 ]; then
                env="$1"
                shift
            else
                env="$(prompt_env)"
            fi
            if [ -z "$env" ]; then
                echo "no env given" >&2
                exit 1
            fi
            exec ./build.sh "$env" "$@"
            ;;
        test)
            # Pokered test suite (ocean/pokered/tests/run_all.sh suite). The tests link the vendor
            # libraries any pokered build produces, so build once if they are missing.
            local suite=ocean/pokered/tests/run_all.sh
            if [ ! -x "$suite" ]; then
                echo "$suite not found (ocean/pokered/tests/ is untracked)" >&2
                exit 1
            fi
            if [ ! -f vendor/gambatte-libretro/install/lib/libgambatte.a ] ||
               [ ! -f vendor/pokered-native/build_native/libnative_core.a ]; then
                ./build.sh pokered || exit 1
            fi
            if [ $# -eq 0 ] && [ -t 0 ]; then
                set -- "$(prompt_tier)"
            fi
            exec "$suite" suite "$@"
            ;;
        *)
            echo "usage: $0 [train|eval|pokered|build|test] [section.key=value ...]" >&2
            exit 1
            ;;
    esac
}

run "$@"
