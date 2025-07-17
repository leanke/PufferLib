import gymnasium
import functools

import pufferlib
import pufferlib.emulation
import pufferlib.pufferlib


def env_creator(name='MountainCarContinuous'):
    return functools.partial(make, name)

def make(name, render_mode='rgb_array', buf=None, seed=None):
    '''Create an environment by name'''
    if name == 'mountaincar-continuous':
        name = 'MountainCarContinuous'
    env = gymnasium.make(name, render_mode=render_mode)
    env = MountainCarWrapper(env)

    env = pufferlib.pufferlib.ClipAction(env)
    env = pufferlib.pufferlib.EpisodeStats(env)
    return pufferlib.emulation.GymnasiumPufferEnv(env=env, buf=buf)

class MountainCarWrapper(gymnasium.Wrapper):
    def step(self, action):
        obs, reward, terminated, truncated, info = self.env.step(action)
        reward = abs(obs[0]+0.5)
        return obs, reward, terminated, truncated, info

