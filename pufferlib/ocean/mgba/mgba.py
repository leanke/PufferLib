import numpy as np
import threading
import json
import multiprocessing
from gymnasium import spaces
import pufferlib
from pufferlib.ocean.mgba import binding

STREAM_COLOR_BLUE = "#0000FF"
STREAM_COLOR_GREEN = "#00A36C"
STREAM_COLOR_RED = "#FF0000"
STREAM_COLOR_PURPLE = "#800080"
STREAM_COLOR_PINK = "#FF00FF"
STREAM_COLOR_YELLOW = "#DAEE01"

WS_URL = "wss://transdimensional.xyz/broadcast" # "ws://localhost:3344/broadcast" #

class mGBA(pufferlib.PufferEnv):
    counter_lock = multiprocessing.Lock()
    counter = multiprocessing.Value('i', 0)
    def __init__(self, num_envs=1, render_mode=None, headless=False, rom_path=None, 
                 frameskip=4, max_episode_length=20480, continuous=False, log_interval=128,
                 stream_enabled=False, stream_user=None, stream_color=None, stream_extra=None,
                 stream_interval=500, buf=None, seed=0):
        with mGBA.counter_lock:
            env_id = mGBA.counter.value
            mGBA.counter.value += 1
        self.env_id = env_id
        self.rom_path = rom_path
        self.frame_skip = frameskip

        self.max_episode_length = max_episode_length
        self.headless = headless
        self.num_agents = num_envs
        self.continuous = continuous
        self.log_interval = log_interval
        self.tick = 0

        self.screen_width = 160
        self.screen_height = 144
        
        self.single_observation_space = spaces.Box(
            low=0, high=255,
            shape=(self.screen_height, self.screen_width, 3),
            dtype=np.float32
        )
        self.single_action_space = spaces.Discrete(9)
        
        super().__init__(buf)
        
        self.c_envs = binding.vec_init(
            self.observations, self.actions, self.rewards,
            self.terminals, self.truncations, num_envs, seed, 
            headless=headless, rom_path=rom_path, 
            frameskip=frameskip, max_episode_length=max_episode_length
        )
        
        self.stream_enabled = stream_enabled
        self.stream_user = stream_user[0] or "User"
        self.stream_color = stream_color[0] or STREAM_COLOR_PURPLE
        self.stream_extra = str(stream_extra)
        self.stream_interval = int(stream_interval)
        self.coords = [[] for _ in range(num_envs)]
        self._ws = None
        self._stream_thread = None
        
        if stream_enabled:
            self._start_stream()
    
    def _start_stream(self):
        try:
            import websockets.sync.client as ws_client
            self._ws = ws_client.connect(WS_URL)
            print(f"Connected to {WS_URL}")
        except Exception as e:
            print(f"Stream connection failed: {e}")
            self.stream_enabled = False
    
    def _broadcast(self):
        if not self._ws:
            return
        try:
            for i, coord_list in enumerate(self.coords):
                if coord_list: 
                    msg = json.dumps({
                        "metadata": {
                            "user": self.stream_user,
                            "color": self.stream_color,
                            "extra": self.stream_extra + f"{self.env_id}-{i+1}" # self.stream_extra,
                            # "env_id": i
                        },
                        "coords": coord_list
                    })
                    self._ws.send(msg)
            self.coords = [[] for _ in range(self.num_agents)]
        except Exception as e:
            print(f"Stream error: {e}")
            self.stream_enabled = False
    
    def reset(self, seed=None):
        self.tick = 0
        binding.vec_reset(self.c_envs, seed or 0)
        return self.observations, []

    def step(self, actions):
        if self.continuous:
            self.actions[:] = np.clip(actions.flatten(), -1.0, 1.0)
        else: 
            self.actions[:] = actions
 
        self.tick += 1
        binding.vec_step(self.c_envs)

        if self.stream_enabled:
            positions = binding.vec_get_positions(self.c_envs)
            for i, (x, y, m) in enumerate(positions):
                if x != 0 or y != 0 or m != 0:
                    self.coords[i].append([int(x), int(y), int(m)])
            
            if self.tick % self.stream_interval == 0:
                self._broadcast()

        info = []
        if self.tick % self.log_interval == 0:
            info.append(binding.vec_log(self.c_envs))

        return (self.observations, self.rewards,
            self.terminals, self.truncations, info)

    def render(self):
        binding.vec_render(self.c_envs, 0)

    def close(self):
        if self._ws:
            try:
                self._ws.close()
            except:
                pass
        binding.vec_close(self.c_envs)
