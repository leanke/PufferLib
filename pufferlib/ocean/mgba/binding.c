#include "mgba.c"
#include "mgba.h"
#include <Python.h>

#define Env mGBA

static int g_env_init_counter = 0;

static PyObject *vec_get_positions(PyObject *self, PyObject *args);

#define MY_METHODS                                                             \
  {"vec_get_positions", vec_get_positions, METH_VARARGS,                       \
   "Get positions of all envs"}

#include "../env_binding.h"

static PyObject *vec_get_positions(PyObject *self, PyObject *args) {
  VecEnv *vec = unpack_vecenv(args);
  if (!vec)
    return NULL;

  PyObject *list = PyList_New(vec->num_envs);
  for (int i = 0; i < vec->num_envs; i++) {
    Env *env = vec->envs[i];
    PyObject *pos = Py_BuildValue("(iii)", env->x, env->y, env->map_n);
    PyList_SetItem(list, i, pos);
  }
  return list;
}

static int my_init(Env *env, PyObject *args, PyObject *kwargs) {
  const char *rom_path = NULL;
  g_env_init_counter++;

  PyObject *rom_path_obj = PyDict_GetItemString(kwargs, "rom_path");
  if (rom_path_obj && rom_path_obj != Py_None) {
    rom_path = PyUnicode_AsUTF8(rom_path_obj);
  }
  if (!rom_path) {
    PyErr_SetString(PyExc_ValueError, "rom_path is required");
    return -1;
  }

  strncpy(env->rom_path, rom_path, sizeof(env->rom_path) - 1);

  FILE *rom_file = fopen(rom_path, "rb");
  if (!rom_file) {
    PyErr_Format(PyExc_FileNotFoundError, "ROM file not found: %s", rom_path);
    return -1;
  }
  fclose(rom_file);

  env->frame_skip = unpack(kwargs, "frameskip");
  env->max_episode_length = unpack(kwargs, "max_episode_length");
  env->render_enabled = !unpack(kwargs, "headless");

  mgba_init_core(env, rom_path);

  if (!env->core) {
    PyErr_SetString(PyExc_RuntimeError, "Failed to initialize mGBA core");
    return -1;
  }

  return 0;
}

static int my_log(PyObject *dict, Log *log) {
  assign_to_dict(dict, "score", log->score);
  assign_to_dict(dict, "episode_return", log->episode_return);
  assign_to_dict(dict, "episode_length", log->episode_length);
  assign_to_dict(dict, "total_steps", log->total_steps);
  assign_to_dict(dict, "prev_badges", log->prev_badges);
  assign_to_dict(dict, "prev_pokemon_count", log->prev_pokemon_count);
  assign_to_dict(dict, "n", log->n);
  return 0;
}
