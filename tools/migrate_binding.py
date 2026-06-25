#!/usr/bin/env python3
"""
migrate_binding.py — Convert old macro-based binding.c to pufferenv.h vtable style.

Usage:
    python tools/migrate_binding.py ocean/cartpole/binding.c
    python tools/migrate_binding.py ocean/*/binding.c        # all envs
"""
import sys
import os
import re

DTYPE_MAP = {
    'FloatTensor':     ('PUFFERENV_DTYPE_FLOAT32', 'sizeof(float)'),
    'ByteTensor':      ('PUFFERENV_DTYPE_UINT8',   'sizeof(uint8_t)'),
    'PrecisionTensor': ('PUFFERENV_DTYPE_FLOAT16',  'sizeof(uint16_t)'),
}


def extract_macro(src, name):
    m = re.search(rf'^\s*#define\s+{name}\s+(.+)', src, re.MULTILINE)
    return m.group(1).strip() if m else None


def extract_act_sizes(src):
    m = re.search(r'#define\s+ACT_SIZES\s+\{([^}]+)\}', src)
    if m:
        return [x.strip() for x in m.group(1).split(',')]
    m = re.search(r'#define\s+NUM_ACT_SIZES\s+(\d+)', src)
    return [None]


def extract_env_type(src):
    """Returns bare struct name from '#define Env Foo'."""
    m = re.search(r'#define\s+Env\s+(\w+)', src)
    return m.group(1) if m else 'Env'


def extract_my_init_body(src):
    """Extract the body of void my_init(Env* env, Dict* kwargs) {...}."""
    m = re.search(r'void\s+my_init\s*\([^)]*\)\s*\{(.*?)\n\}', src, re.DOTALL)
    return m.group(1) if m else ''


def extract_my_log_body(src):
    """Extract the body of void my_log(Log* log, Dict* out) {...}."""
    m = re.search(r'void\s+my_log\s*\([^)]*\)\s*\{(.*?)\n\}', src, re.DOTALL)
    return m.group(1) if m else ''


def migrate_my_init(body, env_type):
    """Convert my_init body from Dict* kwargs → PufferDict* kwargs."""
    body = body.replace('dict_get(kwargs, ', 'pufferenv_get(kwargs, ')
    body = body.replace('->value', '')
    return body


def migrate_my_log(body):
    """Convert my_log body: dict_set(out, ...) → pufferenv_dict_set(out, ...)."""
    body = body.replace('dict_set(out, ', 'pufferenv_dict_set(out, ')
    return body


def generate(src_path):
    with open(src_path) as f:
        src = f.read()

    env_dir = os.path.dirname(src_path)
    env_name = os.path.basename(env_dir)

    obs_size = extract_macro(src, 'OBS_SIZE')
    num_atns = extract_macro(src, 'NUM_ATNS')
    obs_tensor_t = extract_macro(src, 'OBS_TENSOR_T') or 'FloatTensor'
    env_type = extract_env_type(src)
    act_sizes = extract_act_sizes(src)

    env_header = f'{env_name}.h'
    dtype_enum, elem_size_expr = DTYPE_MAP.get(obs_tensor_t, DTYPE_MAP['FloatTensor'])

    act_sizes_str = ', '.join(act_sizes) if act_sizes[0] else '1'
    num_act_sizes = len(act_sizes)

    has_action_mask = 'MY_ACTION_MASK' in src
    action_mask_macro = extract_macro(src, 'MY_ACTION_MASK') if has_action_mask else '0'

    my_init_body = migrate_my_init(extract_my_init_body(src), env_type)
    my_log_body  = migrate_my_log(extract_my_log_body(src))

    has_vec_step = 'MY_VEC_STEP' in src
    vec_step_name = extract_macro(src, 'MY_VEC_STEP') if has_vec_step else None
    vec_step_range_name = extract_macro(src, 'MY_VEC_STEP_RANGE') if has_vec_step else None

    has_perm = 'MY_USES_PERM' in src
    has_tags = 'MY_USES_TAGS' in src

    out_lines = [
        f'#include "pufferenv.h"',
        f'#include "{env_header}"',
        '',
        '// ---- Metadata ---------------------------------------------------------------',
        '',
        f'static int get_obs_size(void)         {{ return {obs_size}; }}',
        f'static int get_num_atns(void)         {{ return {num_atns}; }}',
        f'static const int ACT_SIZES[] = {{{act_sizes_str}}};',
        f'static const int* get_act_sizes(void) {{ return ACT_SIZES; }}',
        f'static int get_num_act_sizes(void)    {{ return {num_act_sizes}; }}',
        f'static PufferEnvDtype get_obs_dtype(void)  {{ return {dtype_enum}; }}',
        f'static size_t get_obs_elem_size(void)      {{ return {elem_size_expr}; }}',
        f'static int get_action_mask_size(void) {{ return {action_mask_macro if has_action_mask else "0"}; }}',
        f'static int uses_perm(void)            {{ return {"1" if has_perm else "0"}; }}',
        f'static int uses_tags(void)            {{ return {"1" if has_tags else "0"}; }}',
        '',
        '// ---- Vec lifecycle ----------------------------------------------------------',
        '',
        'static PufferEnvHandle* vec_init_impl(',
        '        int* num_envs_out, int* buf_starts, int* buf_counts,',
        '        const PufferDict* vec_kwargs, const PufferDict* env_kwargs) {',
        '    // total_agents == num_envs for 1-agent-per-env; adjust for multi-agent envs.',
        '    int num_envs = (int)pufferenv_get(vec_kwargs, "total_agents");',
        '    int num_buffers = (int)pufferenv_get(vec_kwargs, "num_buffers");',
        f'    {env_type}* envs = ({env_type}*)calloc(num_envs, sizeof({env_type}));',
        '    PufferEnvHandle* handles = (PufferEnvHandle*)malloc((size_t)num_envs * sizeof(PufferEnvHandle));',
        '    for (int i = 0; i < num_envs; i++) {',
        '        handles[i] = &envs[i];',
        f'        env_init_impl(handles[i], env_kwargs);',
        '    }',
        '    int per_buf = num_envs / num_buffers;',
        '    for (int b = 0; b < num_buffers; b++) {',
        '        buf_starts[b] = b * per_buf;',
        '        buf_counts[b] = (b == num_buffers - 1) ? num_envs - b * per_buf : per_buf;',
        '    }',
        '    *num_envs_out = num_envs;',
        '    return handles;',
        '}',
        '',
        'static void vec_close_impl(PufferEnvHandle* handles, int num_envs) {',
        '    if (num_envs > 0 && handles[0]) free(handles[0]);',
        '    free(handles);',
        '}',
        '',
        '// ---- Per-env lifecycle ------------------------------------------------------',
        '',
        f'static void env_init_impl(PufferEnvHandle h, const PufferDict* kwargs) {{',
        f'    {env_type}* env = ({env_type}*)h;',
    ]
    for line in my_init_body.split('\n'):
        out_lines.append('   ' + line)
    out_lines += [
        '}',
        '',
        f'static int env_num_agents_impl(PufferEnvHandle h) {{',
        f'    return (({env_type}*)h)->num_agents;',
        '}',
        '',
        f'static void env_step_impl(PufferEnvHandle h)   {{ c_step(({env_type}*)h); }}',
        f'static void env_reset_impl(PufferEnvHandle h)  {{ c_reset(({env_type}*)h); }}',
        f'static void env_render_impl(PufferEnvHandle h) {{ c_render(({env_type}*)h); }}',
        f'static void env_close_impl(PufferEnvHandle h)  {{ c_close(({env_type}*)h); }}',
        '',
        'static void set_slot_impl(PufferEnvHandle h, const PufferEnvSlot* slot) {',
        f'    {env_type}* env = ({env_type}*)h;',
        f'    env->observations = ({obs_tensor_t == "ByteTensor" and "unsigned char*" or "float*"})slot->observations;',
        '    env->actions      = slot->actions;',
        '    env->rewards      = slot->rewards;',
        '    env->terminals    = slot->terminals;',
    ]
    if has_action_mask:
        out_lines.append('    env->action_mask  = slot->action_mask;')
    out_lines += [
        '}',
        '',
        '// ---- Logging ----------------------------------------------------------------',
        '',
        'static void* get_log_ptr_impl(PufferEnvHandle h) {',
        f'    return &(({env_type}*)h)->log;',
        '}',
        '',
        'static int get_log_num_fields_impl(void) {',
        '    return (int)(sizeof(Log) / sizeof(float));',
        '}',
        '',
        'static void log_export_impl(const void* agg, PufferDict* out) {',
        '    const Log* log = (const Log*)agg;',
    ]
    for line in my_log_body.split('\n'):
        out_lines.append('   ' + line)
    out_lines += [
        '}',
        '',
        '// ---- Vtable -----------------------------------------------------------------',
        '',
        'static PufferEnvVTable VTABLE = {',
        '    .abi_version        = PUFFERENV_ABI_VERSION,',
        '    .get_obs_size       = get_obs_size,',
        '    .get_num_atns       = get_num_atns,',
        '    .get_act_sizes      = get_act_sizes,',
        '    .get_num_act_sizes  = get_num_act_sizes,',
        '    .get_obs_dtype      = get_obs_dtype,',
        '    .get_obs_elem_size  = get_obs_elem_size,',
        '    .get_action_mask_size = get_action_mask_size,',
        '    .uses_perm          = uses_perm,',
        '    .uses_tags          = uses_tags,',
        '    .vec_init           = vec_init_impl,',
        '    .vec_close          = vec_close_impl,',
        '    .env_init           = env_init_impl,',
        '    .env_num_agents     = env_num_agents_impl,',
        '    .env_step           = env_step_impl,',
        '    .env_reset          = env_reset_impl,',
        '    .env_render         = env_render_impl,',
        '    .env_close          = env_close_impl,',
        '    .set_slot           = set_slot_impl,',
        f'    .vec_step           = {vec_step_name or "NULL"},',
        f'    .vec_step_range     = {vec_step_range_name or "NULL"},',
        f'    .setup_perm         = {"setup_perm_impl" if has_perm else "NULL"},',
        '    .get_log_ptr        = get_log_ptr_impl,',
        '    .get_log_num_fields = get_log_num_fields_impl,',
        '    .log_export         = log_export_impl,',
        f'    .set_tag             = {"set_tag_impl" if has_tags else "NULL"},',
        f'    .get_tag             = {"get_tag_impl" if has_tags else "NULL"},',
        f'    .get_boundary_reached = {"get_boundary_reached_impl" if has_tags else "NULL"},',
        f'    .set_boundary_reached = {"set_boundary_reached_impl" if has_tags else "NULL"},',
        '    .shared             = NULL,',
        '    .shared_close       = NULL,',
        '    .get                = NULL,',
        '    .put                = NULL,',
        '};',
        '',
        'PufferEnvVTable* pufferenv_get_vtable(void) { return &VTABLE; }',
    ]

    output = '\n'.join(out_lines) + '\n'
    out_path = src_path.replace('binding.c', 'binding_new.c')
    with open(out_path, 'w') as f:
        f.write(output)
    print(f"Written: {out_path}  (review before replacing binding.c)")
    return out_path


def main():
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(1)
    for path in sys.argv[1:]:
        try:
            generate(path)
        except Exception as e:
            print(f"ERROR {path}: {e}")


if __name__ == '__main__':
    main()
