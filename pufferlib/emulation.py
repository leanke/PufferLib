from pdb import set_trace as T

import numpy as np
import warnings

import gymnasium
import inspect

import pufferlib
import pufferlib.spaces
from pufferlib import utils, exceptions


def dtype_from_space(space):
    if isinstance(space, pufferlib.spaces.Tuple):
        dtype = []
        for i, elem in enumerate(space):
            dtype.append((f'f{i}', dtype_from_space(elem)))
    elif isinstance(space, pufferlib.spaces.Dict):
        dtype = []
        for k, value in space.items():
            dtype.append((k, dtype_from_space(value)))
    else:
        dtype = (space.dtype, space.shape)

    return np.dtype(dtype, align=True)

def flatten_space(space):
    if isinstance(space, pufferlib.spaces.Tuple):
        subspaces = []
        for e in space:
            subspaces.extend(flatten_space(e))
        return subspaces
    elif isinstance(space, pufferlib.spaces.Dict):
        subspaces = []
        for e in space.values():
            subspaces.extend(flatten_space(e))
        return subspaces
    else:
        return [space]

def emulate_observation_space(space):
    if isinstance(space, pufferlib.spaces.Box):
        return space, space.dtype

    emulated_dtype = dtype_from_space(space)
    leaves = flatten_space(space)
    dtypes = [e.dtype for e in leaves]
    if dtypes.count(dtypes[0]) == len(dtypes):
        dtype = dtypes[0]
    else:
        dtype = np.dtype(np.uint8)

    mmin, mmax = utils._get_dtype_bounds(dtype)
    numel = emulated_dtype.itemsize // dtype.itemsize
    emulated_space = gymnasium.spaces.Box(low=mmin, high=mmax, shape=(numel,), dtype=dtype)
    return emulated_space, emulated_dtype

def emulate_action_space(space):
    if isinstance(space, pufferlib.spaces.Discrete):
        return space, space.dtype

    emulated_dtype = dtype_from_space(space)
    leaves = flatten_space(space)
    emulated_space = gymnasium.spaces.MultiDiscrete([e.n for e in leaves])
    return emulated_space, emulated_dtype

def emulate(sample, sample_dtype, emulated_dtype):
    emulated = np.zeros(1, dtype=emulated_dtype)
    _emulate(emulated, sample)
    return emulated.view(sample_dtype).ravel()

def _emulate(arr, sample):
    if isinstance(sample, dict):
        for k, v in sample.items():
            _emulate(arr[k], v)
    elif isinstance(sample, tuple):
        for i, v in enumerate(sample):
            _emulate(arr[f'f{i}'], v)
    else:
        arr[()] = sample

def _nativize(sample, space):
    if isinstance(space, pufferlib.spaces.Tuple):
        return tuple(_nativize(sample[f'f{i}'], elem)
            for i, elem in enumerate(space))
    elif isinstance(space, pufferlib.spaces.Dict):
        return {k: _nativize(sample[k], value)
            for k, value in space.items()}
    else:
        return sample.item()

def nativize(sample, sample_space, emulated_dtype):
    sample = np.array(sample).view(emulated_dtype)
    return _nativize(sample, sample_space)

class GymnasiumPufferEnv(gymnasium.Env):
    def __init__(self, env=None, env_creator=None, env_args=[], env_kwargs={}):
        self.env = make_object(env, env_creator, env_args, env_kwargs)

        self.initialized = False
        self.done = True

        self.is_observation_checked = False
        self.is_action_checked = False

        self.observation_space, self.obs_dtype = emulate_observation_space(
            self.env.observation_space)
        self.action_space, self.atn_dtype = emulate_action_space(
            self.env.action_space)
        self.emulated = pufferlib.namespace(
            observation_dtype = self.observation_space.dtype,
            emulated_observation_dtype = self.obs_dtype,
        )

        self.render_modes = 'human rgb_array'.split()
        self.render_mode = 'rgb_array'

    def seed(self, seed):
        self.env.seed(seed)

    def reset(self, seed=None):
        self.initialized = True
        self.done = False

        ob, info = _seed_and_reset(self.env, seed)
        if self.env.observation_space is not self.observation_space:
            ob = emulate(ob, self.observation_space.dtype, self.obs_dtype)

        if __debug__:
            if not self.is_observation_checked:
                self.is_observation_checked = check_space(
                    ob, self.observation_space)

        return ob, info
 
    def step(self, action):
        '''Execute an action and return (observation, reward, done, info)'''
        if not self.initialized:
            raise exceptions.APIUsageError('step() called before reset()')
        if self.done:
            raise exceptions.APIUsageError('step() called after environment is done')

        if __debug__:
            if not self.is_action_checked:
                self.is_action_checked = check_space(
                    action, self.action_space)

        # Unpack actions from multidiscrete into the original action space
        action = nativize(action, self.env.action_space, self.atn_dtype)
        ob, reward, done, truncated, info = self.env.step(action)
        if self.env.observation_space is not self.observation_space:
            ob = emulate(ob, self.observation_space.dtype, self.obs_dtype)
                   
        self.done = done
        return ob, reward, done, truncated, info

    def render(self):
        return self.env.render()

    def close(self):
        return self.env.close()

class PettingZooPufferEnv:
    def __init__(self, env=None, env_creator=None, env_args=[], env_kwargs={}):
        self.env = make_object(env, env_creator, env_args, env_kwargs)
        self.initialized = False
        self.all_done = True

        self.is_observation_checked = False
        self.is_action_checked = False

        # Compute the observation and action spaces
        single_agent = self.possible_agents[0]
        single_observation_space = self.env.observation_space(single_agent)
        single_action_space = self.env.action_space(single_agent)
        self.single_observation_space, self.obs_dtype = (
            emulate_observation_space(single_observation_space))
        self.single_action_space, self.atn_dtype = (
            emulate_action_space(single_action_space))
        self.emulated = pufferlib.namespace(
            observation_dtype = self.single_observation_space.dtype,
            emulated_observation_dtype = self.obs_dtype,
        )

        self.pad_observation = 0 * self.single_observation_space.sample()

    @property
    def agents(self):
        return self.env.agents

    @property
    def possible_agents(self):
        return self.env.possible_agents

    @property
    def done(self):
        return len(self.agents) == 0 or self.all_done

    def observation_space(self, agent):
        '''Returns the observation space for a single agent'''
        if agent not in self.possible_agents:
            raise pufferlib.exceptions.InvalidAgentError(agent, self.possible_agents)

        return self.single_observation_space

    def action_space(self, agent):
        '''Returns the action space for a single agent'''
        if agent not in self.possible_agents:
            raise pufferlib.exceptions.InvalidAgentError(agent, self.possible_agents)

        return self.single_action_space

    def reset(self, seed=None):
        obs, info = self.env.reset(seed=seed)
        self.initialized = True
        self.all_done = False

        # Call user featurizer and flatten the observations
        ob = list(obs.values())[0]
        for agent in self.possible_agents:
            if agent not in obs:
                continue

            ob = obs[agent]
            if self.single_observation_space is not self.single_observation_space:
                ob = emulate(ob, self.single_observation_space.dtype, self.obs_dtype)
            obs[agent] = ob

        if __debug__:
            if not self.is_observation_checked:
                self.is_observation_checked = check_space(
                    next(iter(obs.values())),
                    self.single_observation_space
                )

        padded_obs = pad_agent_data(obs,
            self.possible_agents, self.pad_observation)

        # Mask out missing agents
        padded_infos = {}
        for agent in self.possible_agents:
            if agent not in info:
                padded_infos[agent] = {}
            else:
                padded_infos[agent] = info[agent]
            padded_infos[agent]['mask'] = agent in obs

        return padded_obs, padded_infos

    def step(self, actions):
        '''Step the environment and return (observations, rewards, dones, infos)'''
        if not self.initialized:
            raise exceptions.APIUsageError('step() called before reset()')
        if self.done:
            raise exceptions.APIUsageError('step() called after environment is done')

        # Postprocess actions and validate action spaces
        for agent in actions:
            if __debug__:
                if agent not in self.possible_agents:
                    raise exceptions.InvalidAgentError(agent, self.agents)

        if __debug__:
            if not self.is_action_checked:
                self.is_action_checked = check_space(
                    next(iter(actions.values())),
                    self.single_action_space
                )

        # Unpack actions from multidiscrete into the original action space
        unpacked_actions = {}
        for agent, atn in actions.items():
            if agent not in self.agents:
                continue

            if self.single_action_space is not self.single_action_space:
                atn = nativize(atn, self.single_action_space, self.atn_dtype)

            unpacked_actions[agent] = atn

        obs, rewards, dones, truncateds, infos = self.env.step(unpacked_actions)
        # TODO: Can add this assert once NMMO Horizon is ported to puffer
        # assert all(dones.values()) == (len(self.env.agents) == 0)
        for agent in obs:
            ob = obs[agent] 
            if self.single_observation_space is not self.single_observation_space:
                ob = emulate(ob, self.single_observation_space.dtype, self.obs_dtype)
            obs[agent] = ob
     
        self.all_done = all(dones.values())

        # Mask out missing agents
        for agent in self.possible_agents:
            if agent not in infos:
                infos[agent] = {}
            else:
                infos[agent] = infos[agent]
            infos[agent]['mask'] = agent in obs

        obs = pad_agent_data(obs, self.possible_agents, self.pad_observation)
        rewards = pad_agent_data(rewards, self.possible_agents, 0)
        dones = pad_agent_data(dones, self.possible_agents, False)
        truncateds = pad_agent_data(truncateds, self.possible_agents, False)
        return obs, rewards, dones, truncateds, infos

    def render(self):
        return self.env.render()

    def close(self):
        return self.env.close()

def pad_agent_data(data, agents, pad_value):
    return {agent: data[agent] if agent in data else pad_value
        for agent in agents}
 
def make_object(object_instance=None, object_creator=None, creator_args=[], creator_kwargs={}):
    if (object_instance is None) == (object_creator is None):
        raise ValueError('Exactly one of object_instance or object_creator must be provided')

    if object_instance is not None:
        if callable(object_instance) or inspect.isclass(object_instance):
            raise TypeError('object_instance must be an instance, not a function or class')
        return object_instance

    if object_creator is not None:
        if not callable(object_creator):
            raise TypeError('object_creator must be a callable')
        
        if creator_args is None:
            creator_args = []

        if creator_kwargs is None:
            creator_kwargs = {}

        return object_creator(*creator_args, **creator_kwargs)

def check_space(data, space):
    try:
        contains = space.contains(data)
    except:
        raise exceptions.APIUsageError(
            f'Error checking space {space} with sample :\n{data}')

    if not contains:
        raise exceptions.APIUsageError(
            f'Data:\n{data}\n not in space:\n{space}')
    
    return True

def _seed_and_reset(env, seed):
    if seed is None:
        # Gym bug: does not reset env correctly
        # when seed is passed as explicit None
        return env.reset()

    try:
        obs, info = env.reset(seed=seed)
    except:
        try:
            env.seed(seed)
            obs, info = env.reset()
        except:
            obs, info = env.reset()
            warnings.warn('WARNING: Environment does not support seeding.', DeprecationWarning)

    return obs, info
