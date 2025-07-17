from pdb import set_trace as T

import gymnasium
import functools

import pufferlib.emulation
import pufferlib.environments
import pufferlib.pufferlib


def env_creator(name='car-racing'):
    return functools.partial(make, name=name)

def make(name, domain_randomize=True, continuous=False, render_mode='rgb_array', buf=None, seed=None):
    if name == 'car-racing':
        name = 'CarRacing-v3'

    env = gymnasium.make(name, render_mode=render_mode,
        domain_randomize=domain_randomize, continuous=continuous)
    env = pufferlib.pufferlib.EpisodeStats(env)
    return pufferlib.emulation.GymnasiumPufferEnv(env=env, buf=buf)
