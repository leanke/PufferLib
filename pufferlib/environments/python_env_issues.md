# Python Environment Issues

### butterfly: 
- **Environments:** `cooperative_pong_v5`
- **Status:** FIXED

### classic_control: 
- **Environments:** `cartpole`, `mountaincar`
- **Status:** FIXED

### classic_control_continuous: 
- **Environments:** `mountaincar-continuous`
- **Status:** FIXED

### crafter: 
- **Environments:** `crafter`
- **Status:** FIXED

### links_awaken
- **Status:** TODO - Needs Removed

### mani_skill: 
- **Environments:** `mani_pickcube`, `mani_pushcube`, `mani_stackcube`, `mani_peginsertion`
- **Installation:** `pip install mani_skill`
- **Status:** FIXED

### minigrid:
- **Environments:**  minigrid
- **Status:** WORKS

### mujoco:
- **Environments:**  `HalfCheetah-v4`, `Hopper-v4`, `Swimmer-v4`, `Walker2d-v4`, `Ant-v4`, `Humanoid-v4`, `Reacher-v4`, `InvertedPendulum-v4`, `InvertedDoublePendulum-v4`, `Pusher-v4`, `HumanoidStandup-v4`
- **Status:** WORKS

### slimevolley: 
- **Environments:** `SlimeVolley-v0`
- **Installation:** `pip install slimevolleygym`
- **Status:** FIXED


## Non-Working Environments

### atari
- **Environments:** `adventure`, `air_raid`, `alien`, `amidar`, `assault`, `asterix`, `asteroids`, `atlantis2`, `atlantis`, `backgammon`, `bank_heist`, `basic_math`, `battle_zone`, `beam_rider`, `berzerk`, `blackjack`, `bowling`, `boxing`, `breakout`, `carnival`, `casino`, `centipede`, `chopper_command`, `combat`, `crazy_climber`, `crossbow`, `darkchambers`, `defender`, `demon_attack`, `donkey_kong`, `double_dunk`, `earthworld`, `elevator_action`, `enduro`, `entombed`, `et`, `fishing_derby`, `flag_capture`, `freeway`, `frogger`, `frostbite`, `galaxian`, `gopher`, `gravitar`, `hangman`, `haunted_house`, `hero`, `human_cannonball`, `ice_hockey`, `jamesbond`, `journey_escape`, `joust`, `kaboom`, `kangaroo`, `keystone_kapers`, `king_kong`, `klax`, `koolaid`, `krull`, `kung_fu_master`, `laser_gates`, `lost_luggage`, `mario_bros`, `maze_craze`, `miniature_golf`, `montezuma_revenge`, `mr_do`, `ms_pacman`, `name_this_game`, `othello`, `pacman`, `phoenix`, `pitfall2`, `pitfall`, `pong`, `pooyan`, `private_eye`, `qbert`, `riverraid`, `road_runner`, `robotank`, `seaquest`, `sir_lancelot`, `skiing`, `solaris`, `space_invaders`, `space_war`, `star_gunner`, `superman`, `surround`, `tennis`, `tetris`, `tic_tac_toe_3d`, `time_pilot`, `trondead`, `turmoil`, `tutankham`, `up_n_down`, `venture`, `video_checkers`, `video_chess`, `video_cube`, `video_pinball`, `warlords`, `wizard_of_wor`, `word_zapper`, `yars_revenge`, `zaxxon`
- **Issue:** `RuntimeError: mat1 and mat2 shapes cannot be multiplied (64x4608 and 3456x512)`

### box2d
- **Environments:** `car-racing`
- **Installation:** 
  ```bash
  sudo apt install swig
  pip install box2d
  ```
- **Issue:** Invalid action space error
<details>
<summary>Full error details</summary>

```
gymnasium.error.InvalidAction: you passed the invalid action `3.0`. 
The supported action_space is `Discrete(5)`
```
</details>

### bsuite
- **Environments:** `bandit/0`
- **Installation:** `pip install bsuite`
- **Issue:** Python 3.12 compatibility - `imp` module removed
- **Solution:** Use older Python version (3.8-3.11) or wait for upstream fix

<details>
<summary>Full error details</summary>

```
ModuleNotFoundError: No module named 'imp'
```
</details>


### craftax
- **Environments:** `Craftax-Symbolic-v1`, `Craftax-Classic-Symbolic-v1`
- **Installation:** `pip install jax[cuda] gymnax`
- **Issue:** `ModuleNotFoundError: No module named 'gymnax.spaces'`



### dm_control
- **Environments:** `dmc`
- **Issue:** `ValueError: Domain 'dmc' does not exist.`

<details>
<summary>Full error details</summary>

```
│ PufferLib/pufferlib/environments/dm_control/environment.py:22 in                                 │
│ make                                                                                             │
│                                                                                                  │
│   19 def make(name, task_name='walk', buf=None, seed=None):                                      │
│   20 │   '''Untested. Let us know in Discord if you want to use dmc in PufferLib.'''             │
│   21 │   dm_control = pufferlib.environments.try_import('dm_control.suite', 'dmc')               │
│ ❱ 22 │   env = dm_control.suite.load(name, task_name)                                            │
│   23 │   env = shimmy.DmControlCompatibilityV0(env=env)                                          │
│   24 │   return pufferlib.emulation.GymnasiumPufferEnv(env, buf=buf)                             │
│   25                                                                                             │
│                                                                                                  │
│ .pyenv/versions/3.12.7/envs/fixing/lib/python3.12/site-packages/dm_control/suite/__              │
│ init__.py:113 in load                                                                            │
│                                                                                                  │
│   110   Returns:                                                                                 │
│   111 │   The requested environment.                                                             │
│   112   """                                                                                      │
│ ❱ 113   return build_environment(domain_name, task_name, task_kwargs,                            │
│   114 │   │   │   │   │   │      environment_kwargs, visualize_reward)                           │
│   115                                                                                            │
│   116                                                                                            │
│                                                                                                  │
│ .pyenv/versions/3.12.7/envs/fixing/lib/python3.12/site-packages/dm_control/suite/__              │
│ init__.py:137 in build_environment                                                               │
│                                                                                                  │
│   134 │   An instance of the requested environment.                                              │
│   135   """                                                                                      │
│   136   if domain_name not in _DOMAINS:                                                          │
│ ❱ 137 │   raise ValueError('Domain {!r} does not exist.'.format(domain_name))                    │
│   138                                                                                            │
│   139   domain = _DOMAINS[domain_name]                                                           │
│   140                                                                                            │
╰──────────────────────────────────────────────────────────────────────────────────────────────────╯
ValueError: Domain 'dmc' does not exist.
```
</details>

### dm_lab
- **Environments:** `dml`
- **Issue:** `ImportError: No module named 'deepmind_lab'`
<details>
<summary>Full error details</summary>

```
This is probably an installation error. Try: `pip install pufferlib[dm-lab]`. Note that some environments have non-python dependencies. These are included in PufferTank. Or, you can install manually by following the instructions provided by the environment maintainers. But some are finicky, so we recommend using PufferTank.
```
</details>

### gpudrive
- **Environments:** `gpudrive`
- **Issue:** `AttributeError: 'str' object has no attribute 'setdefault'`

<details>
<summary>Full error details</summary>

```
╭─────────────────────────────── Traceback (most recent call last) ────────────────────────────────╮
│ .pyenv/versions/fixing/bin/puffer:8 in <module>                                                  │
│                                                                                                  │
│   5 from pufferlib.pufferl import main                                                           │
│   6 if __name__ == '__main__':                                                                   │
│   7 │   sys.argv[0] = re.sub(r'(-script\.pyw|\.exe)?$', '', sys.argv[0])                         │
│ ❱ 8 │   sys.exit(main())                                                                         │
│   9                                                                                              │
│                                                                                                  │
│ PufferLib/pufferlib/pufferl.py:1199 in main                                                      │
│                                                                                                  │
│   1196 │   mode = sys.argv.pop(1)                                                                │
│   1197 │   env_name = sys.argv.pop(1)                                                            │
│   1198 │   if mode == 'train':                                                                   │
│ ❱ 1199 │   │   train(env_name=env_name)                                                          │
│   1200 │   elif mode == 'eval':                                                                  │
│   1201 │   │   eval(env_name=env_name)                                                           │
│   1202 │   elif mode == 'sweep':                                                                 │
│                                                                                                  │
│ PufferLib/pufferlib/pufferl.py:867 in train                                                      │
│                                                                                                  │
│    864 │   │   return f'{data_dir}/{model_file}'                                                 │
│    865                                                                                           │
│    866 def train(env_name, args=None, vecenv=None, policy=None, logger=None):                    │
│ ❱  867 │   args = args or load_config(env_name)                                                  │
│    868 │                                                                                         │
│    869 │   # Assume TorchRun DDP is used if LOCAL_RANK is set                                    │
│    870 │   if 'LOCAL_RANK' in os.environ:                                                        │
│                                                                                                  │
│ PufferLib/pufferlib/pufferl.py:1184 in load_config                                               │
│                                                                                                  │
│   1181 │   │   next = args                                                                       │
│   1182 │   │   for subkey in key.split('.'):                                                     │
│   1183 │   │   │   prev = next                                                                   │
│ ❱ 1184 │   │   │   next = next.setdefault(subkey, {})                                            │
│   1185 │   │                                                                                     │
│   1186 │   │   prev[subkey] = value                                                              │
│   1187                                                                                           │
╰──────────────────────────────────────────────────────────────────────────────────────────────────╯
AttributeError: 'str' object has no attribute 'setdefault'
```
</details>

### griddly
- **Environments:** `spiders`
- **Issue:** `ImportError: No module named 'griddly'`
<details>
<summary>Full error details</summary>

```
This is probably an installation error. Try: `pip install pufferlib[griddly]`. 
Note that some environments have non-python dependencies. These are included in PufferTank. 
Or, you can install manually by following the instructions provided by the environment maintainers. 
But some are finicky, so we recommend using PufferTank.
```
</details>

### gvgai
- **Environments:** `zelda`
- **Issue:** `AttributeError: 'str' object has no attribute 'setdefault'`

<details>
<summary>Full error details</summary>

```
╭─────────────────────────────── Traceback (most recent call last) ────────────────────────────────╮
│ .pyenv/versions/fixing/bin/puffer:8 in <module>                                                  │
│                                                                                                  │
│   5 from pufferlib.pufferl import main                                                           │
│   6 if __name__ == '__main__':                                                                   │
│   7 │   sys.argv[0] = re.sub(r'(-script\.pyw|\.exe)?$', '', sys.argv[0])                         │
│ ❱ 8 │   sys.exit(main())                                                                         │
│   9                                                                                              │
│                                                                                                  │
│ PufferLib/pufferlib/pufferl.py:1199 in main                                                      │
│                                                                                                  │
│   1196 │   mode = sys.argv.pop(1)                                                                │
│   1197 │   env_name = sys.argv.pop(1)                                                            │
│   1198 │   if mode == 'train':                                                                   │
│ ❱ 1199 │   │   train(env_name=env_name)                                                          │
│   1200 │   elif mode == 'eval':                                                                  │
│   1201 │   │   eval(env_name=env_name)                                                           │
│   1202 │   elif mode == 'sweep':                                                                 │
│                                                                                                  │
│ PufferLib/pufferlib/pufferl.py:867 in train                                                      │
│                                                                                                  │
│    864 │   │   return f'{data_dir}/{model_file}'                                                 │
│    865                                                                                           │
│    866 def train(env_name, args=None, vecenv=None, policy=None, logger=None):                    │
│ ❱  867 │   args = args or load_config(env_name)                                                  │
│    868 │                                                                                         │
│    869 │   # Assume TorchRun DDP is used if LOCAL_RANK is set                                    │
│    870 │   if 'LOCAL_RANK' in os.environ:                                                        │
│                                                                                                  │
│ PufferLib/pufferlib/pufferl.py:1184 in load_config                                               │
│                                                                                                  │
│   1181 │   │   next = args                                                                       │
│   1182 │   │   for subkey in key.split('.'):                                                     │
│   1183 │   │   │   prev = next                                                                   │
│ ❱ 1184 │   │   │   next = next.setdefault(subkey, {})                                            │
│   1185 │   │                                                                                     │
│   1186 │   │   prev[subkey] = value                                                              │
│   1187                                                                                           │
╰──────────────────────────────────────────────────────────────────────────────────────────────────╯
AttributeError: 'str' object has no attribute 'setdefault'
```
</details>

### kinetix
- **Environments:** `kinetix-pixels-discrete`, `kinetix-symbolic-discrete`
- **Issue:** `AttributeError: 'str' object has no attribute 'setdefault'`
<details>
<summary>Full error details</summary>

```
╭─────────────────────────────── Traceback (most recent call last) ────────────────────────────────╮
│ .pyenv/versions/fixing/bin/puffer:8 in <module>                                                  │
│                                                                                                  │
│   5 from pufferlib.pufferl import main                                                           │
│   6 if __name__ == '__main__':                                                                   │
│   7 │   sys.argv[0] = re.sub(r'(-script\.pyw|\.exe)?$', '', sys.argv[0])                         │
│ ❱ 8 │   sys.exit(main())                                                                         │
│   9                                                                                              │
│                                                                                                  │
│ PufferLib/pufferlib/pufferl.py:1199 in main                                                      │
│                                                                                                  │
│   1196 │   mode = sys.argv.pop(1)                                                                │
│   1197 │   env_name = sys.argv.pop(1)                                                            │
│   1198 │   if mode == 'train':                                                                   │
│ ❱ 1199 │   │   train(env_name=env_name)                                                          │
│   1200 │   elif mode == 'eval':                                                                  │
│   1201 │   │   eval(env_name=env_name)                                                           │
│   1202 │   elif mode == 'sweep':                                                                 │
│                                                                                                  │
│ PufferLib/pufferlib/pufferl.py:867 in train                                                      │
│                                                                                                  │
│    864 │   │   return f'{data_dir}/{model_file}'                                                 │
│    865                                                                                           │
│    866 def train(env_name, args=None, vecenv=None, policy=None, logger=None):                    │
│ ❱  867 │   args = args or load_config(env_name)                                                  │
│    868 │                                                                                         │
│    869 │   # Assume TorchRun DDP is used if LOCAL_RANK is set                                    │
│    870 │   if 'LOCAL_RANK' in os.environ:                                                        │
│                                                                                                  │
│ PufferLib/pufferlib/pufferl.py:1184 in load_config                                               │
│                                                                                                  │
│   1181 │   │   next = args                                                                       │
│   1182 │   │   for subkey in key.split('.'):                                                     │
│   1183 │   │   │   prev = next                                                                   │
│ ❱ 1184 │   │   │   next = next.setdefault(subkey, {})                                            │
│   1185 │   │                                                                                     │
│   1186 │   │   prev[subkey] = value                                                              │
│   1187                                                                                           │
╰──────────────────────────────────────────────────────────────────────────────────────────────────╯
AttributeError: 'str' object has no attribute 'setdefault'
```
</details>

### magent
- **Environments:** `battle_v4`
- **Issue:** ImportError: MAgent has been moved into its own package: MAgent2. Install with `pip install magent2`. For more information on the MAgent2 package, see https://magent2.farama.org/.
<details>
<summary>Full error details</summary>

```
│ PufferLib/pufferlib/environments/magent/environment.py:17 in                                     │
│ make                                                                                             │
│                                                                                                  │
│   14 def make(name, buf=None):                                                                   │
│   15 │   '''MAgent Battle V4 creation function'''                                                │
│   16 │   if name == 'battle_v4':                                                                 │
│ ❱ 17 │   │   from pettingzoo.magent import battle_v4                                             │
│   18 │   │   env_cls = battle_v4.env                                                             │
│   19 │   else:                                                                                   │
│   20 │   │   raise ValueError(f'Unknown environment name {name}')                                │
│                                                                                                  │
│ .pyenv/versions/3.12.7/envs/fixing/lib/python3.12/site-packages/pettingzoo/magent/_              │
│ _init__.py:1 in <module>                                                                         │
│                                                                                                  │
│ ❱ 1 raise ImportError(                                                                           │
│   2 │   "MAgent has been moved into its own package: MAgent2. Install with `pip install mage     │
│   3 )                                                                                            │
│   4                                                                                              │
╰──────────────────────────────────────────────────────────────────────────────────────────────────╯
ImportError: MAgent has been moved into its own package: MAgent2. Install with `pip install 
magent2`. For more information on the MAgent2 package, see https://magent2.farama.org/.
```
</details>

### metta
- **Environments:** `metta`
- **Installation:** 
  ```bash
  pip install omegaconf
  pip install metta
  ```
- **Issue:** Build dependencies error - `pyarrow<3.0.0,>=2.0.0` fails to install due to numpy compatibility issues with Python 3.12

<details>
<summary>Full error details</summary>

```
Collecting pyarrow<3.0.0,>=2.0.0 (from metta)
  Downloading pyarrow-2.0.0.tar.gz (58.9 MB)
  Installing build dependencies ... error
  
  × pip subprocess to install build dependencies did not run successfully.
  │ exit code: 1
  
  × Encountered error while generating package metadata.
  note: This is an issue with the package mentioned above, not pip.
```
</details>

### microrts
- **Environments:** `GlobalAgentCombinedRewardEnv`
- **Issue:** Never stepped

### minerl
- **Environments:** `MineRLNavigateDense-v0`
- **Issue:** `ImportError: No module named 'minerl'`
<details>
<summary>Full error details</summary>

```
This is probably an installation error. Try: `pip install pufferlib[minerl]`. 
Note that some environments have non-python dependencies. These are included in PufferTank. 
Or, you can install manually by following the instructions provided by the environment maintainers. 
But some are finicky, so we recommend using PufferTank.
```
</details>

### minihack
- **Environments:** `minihack`
- **Installation:** `pip install minihack`
- **Issue:** `NameNotFound: Environment 'MiniHack-River' doesn't exist.`

### nethack
- **Environments:** `nethack`
- **Issue:** `NameNotFound: Environment 'NetHackScore' doesn't exist.`

### nmmo
- **Environments:** `nmmo`
- **Issue:** `ImportError: No module named 'nmmo'`
<details>
<summary>Full error details</summary>

```
This is probably an installation error. Try: `pip install pufferlib[nmmo]`. 
Note that some environments have non-python dependencies. These are included in PufferTank. 
Or, you can install manually by following the instructions provided by the environment maintainers. 
But some are finicky, so we recommend using PufferTank.
```
</details>

### open_spiel
- **Environments:** `connect_four`
- **Issue:** `ImportError: No module named 'pyspiel'`
<details>
<summary>Full error details</summary>

```
This is probably an installation error. Try: `pip install pufferlib[open_spiel]`. 
Note that some environments have non-python dependencies. These are included in PufferTank. 
Or, you can install manually by following the instructions provided by the environment maintainers. 
But some are finicky, so we recommend using PufferTank.
```
</details>

### pokemon_red
- **Environments:** `pokemon_red`
- **Issue:** Needs updated 

### procgen
- **Environments:** `bigfish`, `bossfight`, `caveflyer`, `chaser`, `climber`, `coinrun`, `dodgeball`, `fruitbot`, `heist`, `jumper`, `leaper`, `maze`, `miner`, `ninja`, `plunder`, `starpilot`
- **Issue:** `ModuleNotFoundError: No module named 'gymnasium.wrappers.monitoring'`
From environment.py importing SB3

### smac
- **Environments:** (see starcraft.ini)
- **Status:** TODO - starcraft.ini has smac as package

### stable_retro
- **Environments:** `Airstriker-Genesis`
- **Issue:** `ImportError: No module named 'retro'`
<details>
<summary>Full error details</summary>

```
This is probably an installation error. Try: `pip install pufferlib[stable-retro]`. 
Note that some environments have non-python dependencies. These are included in PufferTank. 
Or, you can install manually by following the instructions provided by the environment maintainers. 
But some are finicky, so we recommend using PufferTank.
```
</details>

### trade_sim
- **Environments:** `trade_sim`
- **Status:** TODO - something about metta

### vizdoom
- **Environments:** `doom`
- **Issue:** `ValueError: Wrong screen resolution.`
<details>
<summary>Full error details</summary>

```
│ PufferLib/pufferlib/environments/vizdoom/environment.py:36 in                                    │
│ make                                                                                             │
│                                                                                                  │
│   33 │   with pufferlib.pufferlib.Suppress():                                                    │
│   34 │   │   env = gym.make(name, render_mode=render_mode)                                       │
│   35 │                                                                                           │
│ ❱ 36 │   env = DoomWrapper(env) # Don't use standard postprocessor                               │
│   37 │                                                                                           │
│   38 │   #env = gym.wrappers.RecordEpisodeStatistics(env)                                        │
│   39 │   #env = NoopResetEnv(env, noop_max=30)                                                   │
│                                                                                                  │
│ PufferLib/pufferlib/environments/vizdoom/environment.py:55 in                                    │
│ __init__                                                                                         │
│                                                                                                  │
│   52 │   def __init__(self, env):                                                                │
│   53 │   │   super().__init__(env.unwrapped)                                                     │
│   54 │   │   if env.observation_space['screen'].shape[0] != 120:                                 │
│ ❱ 55 │   │   │   raise ValueError('Wrong screen resolution. Doom does not provide '              │
│   56 │   │   │   │   'a way to change this. You must edit scenarios/<env_name>.cfg'              │
│   57 │   │   │   │   'This is inside your local ViZDoom installation. Likely in python system    │
│   58 │   │   │   │   'Set screen resolution to RES_160X120 and screen format to GRAY8')          │
╰──────────────────────────────────────────────────────────────────────────────────────────────────╯
ValueError: Wrong screen resolution. Doom does not provide a way to change this. You must edit 
scenarios/<env_name>.cfgThis is inside your local ViZDoom installation. Likely in python system 
packagesSet screen resolution to RES_160X120 and screen format to GRAY8
```
</details>




## Pip List

<details>
<summary>Packages</summary>

```

Package                    Version        Editable project location
-------------------------- -------------- -----------------------------------------
absl-py                    2.3.1
ale-py                     0.9.0
annotated-types            0.7.0
antlr4-python3-runtime     4.9.3
arm_pytorch_utilities      0.4.3
arrow                      1.3.0
asttokens                  3.0.0
attrs                      25.3.0
AutoROM                    0.4.2
AutoROM.accept-rom-license 0.6.1
boto3                      1.39.7
botocore                   1.39.7
Box2D                      2.3.10
bravado                    11.1.0
bravado-core               6.1.1
certifi                    2025.7.14
cffi                       1.17.1
charset-normalizer         3.4.2
chex                       0.1.89
click                      8.2.1
cloudpickle                3.1.1
contourpy                  1.3.2
craftax                    1.5.0
crafter                    1.8.3
cycler                     0.12.1
dacite                     1.9.2
decorator                  5.2.1
dm-control                 1.0.11
dm-env                     1.6
dm-tree                    0.1.9
docstring_parser           0.16
einops                     0.6.1
etils                      1.13.0
executing                  2.2.0
Farama-Notifications       0.0.4
fast_kinematics            0.2.2
ffmpeg                     1.4
filelock                   3.18.0
flax                       0.10.7
fonttools                  4.59.0
fqdn                       1.5.1
fsspec                     2025.7.0
future                     1.0.0
gitdb                      4.0.12
GitPython                  3.1.44
glcontext                  3.0.0
glfw                       1.12.0
gym                        0.23.0
gym-deepmindlab            0.1.2
gym_microrts               0.3.2
gym-notices                0.0.8
gym3                       0.3.3
gymnasium                  1.0.0
gymnax                     0.0.9
h5py                       3.14.0
heavyball                  1.7.2
hf-xet                     1.1.5
hnswlib                    0.7.0
huggingface-hub            0.33.4
humanize                   4.12.3
idna                       3.10
imageio                    2.37.0
imageio-ffmpeg             0.3.0
importlib_resources        6.5.2
ipython                    9.4.0
ipython_pygments_lexers    1.1.1
isoduration                20.11.0
jax                        0.6.2
jax-cuda12-pjrt            0.6.2
jax-cuda12-plugin          0.6.2
jaxlib                     0.6.2
jedi                       0.19.2
Jinja2                     3.1.6
jmespath                   1.0.1
jpype1                     1.6.0
jsonpointer                3.0.0
jsonref                    1.1.0
jsonschema                 4.24.0
jsonschema-specifications  2025.4.1
kiwisolver                 1.4.8
labmaze                    1.0.6
lazy_loader                0.4
lxml                       6.0.0
mani_skill                 3.0.0b21
markdown-it-py             3.0.0
MarkupSafe                 3.0.2
matplotlib                 3.10.3
matplotlib-inline          0.1.7
mdurl                      0.1.2
mediapy                    1.2.4
minigrid                   2.3.1
minihack                   1.0.2
ml_dtypes                  0.5.1
moderngl                   5.12.0
monotonic                  1.6
mplib                      0.1.1
mpmath                     1.3.0
msgpack                    1.1.1
mujoco                     3.3.4
neptune                    1.14.0
nest-asyncio               1.6.0
networkx                   3.5
nle                        1.2.0
numpy                      1.26.4
nvidia-cublas-cu12         12.6.4.1
nvidia-cuda-cupti-cu12     12.6.80
nvidia-cuda-nvcc-cu12      12.9.86
nvidia-cuda-nvrtc-cu12     12.6.77
nvidia-cuda-runtime-cu12   12.6.77
nvidia-cudnn-cu12          9.11.0.98
nvidia-cufft-cu12          11.3.0.4
nvidia-cufile-cu12         1.11.1.6
nvidia-curand-cu12         10.3.7.77
nvidia-cusolver-cu12       11.7.1.2
nvidia-cusparse-cu12       12.5.4.2
nvidia-cusparselt-cu12     0.6.3
nvidia-ml-py               12.575.51
nvidia-nccl-cu12           2.26.2
nvidia-nvjitlink-cu12      12.6.85
nvidia-nvshmem-cu12        3.3.9
nvidia-nvtx-cu12           12.6.77
oauthlib                   3.3.1
omegaconf                  2.3.0
opencv-python              4.11.0.86
opensimplex                0.4.5.1
opt_einsum                 3.4.0
optax                      0.2.5
orbax-checkpoint           0.11.19
packaging                  25.0
pandas                     2.0.2
parso                      0.8.4
pettingzoo                 1.24.1
pexpect                    4.9.0
pillow                     11.3.0
pip                        24.2
platformdirs               4.3.8
pokegym                    0.2.0
procgen-mirror             0.10.7
prompt_toolkit             3.0.51
protobuf                   6.31.1
psutil                     7.0.0
ptyprocess                 0.7.0
pufferlib                  3.0.0          PufferLib
pure_eval                  0.2.3
pybind11                   3.0.0
pyboy                      1.6.14
pycparser                  2.22
pydantic                   2.11.7
pydantic_core              2.33.2
pygame                     2.6.1
pyglet                     1.5.11
Pygments                   2.19.2
PyJWT                      2.10.1
pynvml                     12.0.0
PyOpenGL                   3.1.9
pyparsing                  3.2.3
pyperclip                  1.9.0
pyro-api                   0.1.2
pyro-ppl                   1.9.1
PySDL2                     0.9.17
pysdl2-dll                 2.32.0
python-dateutil            2.9.0.post0
pytorch-kinematics         0.7.5
pytorch-seed               0.2.0
pytz                       2025.2
PyYAML                     6.0.2
referencing                0.36.2
requests                   2.32.4
requests-oauthlib          2.0.0
rfc3339-validator          0.1.4
rfc3986-validator          0.1.1
rich                       14.0.0
rich-argparse              1.7.1
rpds-py                    0.26.0
ruamel.yaml                0.18.14
ruamel.yaml.clib           0.2.12
s3transfer                 0.13.0
sapien                     3.0.0b1
scikit-image               0.25.2
scipy                      1.16.0
seaborn                    0.13.2
sentry-sdk                 2.33.0
setuptools                 80.9.0
Shimmy                     1.3.0
shtab                      1.7.2
simplejson                 3.20.1
six                        1.17.0
slimevolleygym             0.1.0
smmap                      5.0.2
stable-baselines3          2.1.0
stack-data                 0.6.3
swagger-spec-validator     3.0.4
swig                       4.3.1
sympy                      1.14.0
tabulate                   0.9.0
tensorstore                0.1.76
tifffile                   2025.6.11
toolz                      1.0.0
toppra                     0.6.3
torch                      2.7.1
torchvision                0.22.1
tqdm                       4.67.1
traitlets                  5.14.3
transforms3d               0.4.2
treescope                  0.1.9
trimesh                    4.7.1
triton                     3.3.1
typeguard                  4.4.4
types-python-dateutil      2.9.0.20250708
typing_extensions          4.14.1
typing-inspection          0.4.1
tyro                       0.9.26
tzdata                     2025.2
uri-template               1.3.0
urllib3                    2.5.0
vizdoom                    1.2.3
wandb                      0.21.0
wcwidth                    0.2.13
webcolors                  24.11.1
websocket-client           1.8.0
websockets                 15.0.1
wrapt                      1.17.2
zipp                       3.23.0
```
</details>