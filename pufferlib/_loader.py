import ctypes
import os

_RTLD_GLOBAL = getattr(os, 'RTLD_GLOBAL', ctypes.RTLD_GLOBAL)
_RTLD_NOW    = getattr(os, 'RTLD_NOW',    2)  # 2 = RTLD_NOW on POSIX

_loaded_libs = {}

def find_env_plugin(env_name, search_dirs=None):
    filename = f"{env_name}_env.so"
    paths = list(search_dirs or [])
    env_path_var = os.environ.get('PUFFERLIB_ENV_PATH', '')
    if env_path_var:
        paths.extend(env_path_var.split(':'))
    paths.append(os.path.expanduser('~/.local/share/pufferlib/envs'))
    repo_dir = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    paths.append(os.path.join(repo_dir, 'ocean', env_name))
    for d in paths:
        p = os.path.join(d, filename)
        if os.path.exists(p):
            return p
    raise FileNotFoundError(
        f"No plugin found for '{env_name}'. "
        f"Build with: ./build_env.sh {env_name}"
    )

def load_env_plugin(path):
    if path not in _loaded_libs:
        lib = ctypes.CDLL(path, mode=_RTLD_GLOBAL | _RTLD_NOW)
        _loaded_libs[path] = lib
    lib = _loaded_libs[path]
    lib.pufferenv_get_vtable.restype = ctypes.c_void_p
    return lib, int(lib.pufferenv_get_vtable())
