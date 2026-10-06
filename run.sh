#!/usr/bin/env bash

export CUDA_VISIBLE_DEVICES=0
export NVCC_ARCH=sm_86

NCCL_LIB=$(python -c 'import nvidia.nccl, os; print(os.path.join(nvidia.nccl.__path__[0], "lib"))')
export LD_LIBRARY_PATH="$NCCL_LIB:${LD_LIBRARY_PATH:-}"

prompt_mode() {
    local choice
    echo "Select a run mode:" >&2
    echo "  1) ./puffer train" >&2
    echo "  2) ./puffer eval" >&2
    echo "  3) ./pokered" >&2
    echo "  4) ./build.sh" >&2
    read -r -p "Enter 1-4: " choice
    case "$choice" in
        1) echo "train" ;;
        2) echo "eval" ;;
        3) echo "pokered" ;;
        4) echo "build" ;;
        *)
            echo "invalid choice: $choice" >&2
            exit 1
            ;;
    esac
}

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

prompt_total_agents() {
    local answer
    read -r -p "vec.total_agents [leave blank for default]: " answer
    echo "$answer"
}

run() {
    local mode="$1"
    shift
    if [ -z "$mode" ]; then
        mode="$(prompt_mode)"
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
            ./build.sh pokered
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
        *)
            echo "usage: $0 [train|eval|pokered|build] [section.key=value ...]" >&2
            exit 1
            ;;
    esac
}

run "$@"
