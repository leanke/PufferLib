#include "mgba.h"
#include "mgba_core.h"
#include <Python.h>

#define Env mGBA
#define MAX_MAPS 256
#define MAX_X 256
#define MAX_Y 256
#define VISITED_COORDS_SIZE (MAX_MAPS * MAX_X * MAX_Y)

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
    PyObject *pos = Py_BuildValue("(iii)", env-> ram.x, env->ram.y, env->ram.map_n);
    PyList_SetItem(list, i, pos);
  }
  return list;
}

static int my_init(Env *env, PyObject *args, PyObject *kwargs) {
  const char *rom_path = NULL;
  g_env_init_counter++;
  env->emu.frame_skip = unpack(kwargs, "frameskip");
  env->max_episode_length = unpack(kwargs, "max_episode_length");
  env->emu.render_enabled = !unpack(kwargs, "headless");
  env->full_reset = unpack(kwargs, "full_reset");

  PyObject *state_path_obj = PyDict_GetItemString(kwargs, "state_path");
  if (state_path_obj && state_path_obj != Py_None) {
    const char *state_path = PyUnicode_AsUTF8(state_path_obj);
    strncpy(env->emu.state_path, state_path, sizeof(env->emu.state_path) - 1);
  }

  PyObject *rom_path_obj = PyDict_GetItemString(kwargs, "rom_path");
  if (rom_path_obj && rom_path_obj != Py_None) {
    rom_path = PyUnicode_AsUTF8(rom_path_obj);
  }
  if (!rom_path) {
    PyErr_SetString(PyExc_ValueError, "rom_path is required");
    return -1;
  }
  strncpy(env->emu.rom_path, rom_path, sizeof(env->emu.rom_path) - 1);
  FILE *rom_file = fopen(rom_path, "rb");
  if (!rom_file) {
    PyErr_Format(PyExc_FileNotFoundError, "ROM file not found: %s", rom_path);
    return -1;
  }
  fclose(rom_file);


  mgba_init_core(&env->emu, rom_path);
  env->visited_coords = (uint8_t *)calloc(VISITED_COORDS_SIZE, sizeof(uint8_t));
  env->prev_visited_coords = (uint8_t *)calloc(VISITED_COORDS_SIZE, sizeof(uint8_t));
  memset(env->prev_visited_coords, 1, VISITED_COORDS_SIZE);
  env->unique_coords_count = 0;

  if (!env->emu.core) {
    PyErr_SetString(PyExc_RuntimeError, "Failed to initialize mGBA core");
    return -1;
  }

  return 0;
}

static int my_log(PyObject *dict, Log *log) {
  assign_to_dict(dict, "episode_length", log->episode_length);
  assign_to_dict(dict, "episode_return", log->episode_return);
  assign_to_dict(dict, "total_steps", log->total_steps);
  assign_to_dict(dict, "unique_coords", log->unique_coords);
  assign_to_dict(dict, "level_sum", log->level_sum);

  assign_to_dict(dict, "badges", log->badges);
  assign_to_dict(dict, "money", log->money);
  assign_to_dict(dict, "party_count", log->party_count);
  assign_to_dict(dict, "pkmn1_lvl", log->pkmn1_lvl);
  assign_to_dict(dict, "pkmn2_lvl", log->pkmn2_lvl);
  assign_to_dict(dict, "pkmn3_lvl", log->pkmn3_lvl);
  assign_to_dict(dict, "pkmn4_lvl", log->pkmn4_lvl);
  assign_to_dict(dict, "pkmn5_lvl", log->pkmn5_lvl);
  assign_to_dict(dict, "pkmn6_lvl", log->pkmn6_lvl);

  assign_to_dict(dict, "n", log->n);
  return 0;
}
