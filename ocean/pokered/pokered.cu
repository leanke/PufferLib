#define PKR_ENC_C1_IC 1
#define PKR_ENC_C1_OC 32
#define PKR_ENC_C1_K 8
#define PKR_ENC_C1_S 4
#define PKR_ENC_C1_OH ((SCALED_HEIGHT - PKR_ENC_C1_K) / PKR_ENC_C1_S + 1)
#define PKR_ENC_C1_OW ((SCALED_WIDTH - PKR_ENC_C1_K) / PKR_ENC_C1_S + 1)
#define PKR_ENC_C1_SPATIAL (PKR_ENC_C1_OH * PKR_ENC_C1_OW)
#define PKR_ENC_C1_COL_W (PKR_ENC_C1_IC * PKR_ENC_C1_K * PKR_ENC_C1_K)

#define PKR_ENC_C2_IC PKR_ENC_C1_OC
#define PKR_ENC_C2_OC 64
#define PKR_ENC_C2_K 3
#define PKR_ENC_C2_S 2
#define PKR_ENC_C2_OH ((PKR_ENC_C1_OH - PKR_ENC_C2_K) / PKR_ENC_C2_S + 1)
#define PKR_ENC_C2_OW ((PKR_ENC_C1_OW - PKR_ENC_C2_K) / PKR_ENC_C2_S + 1)
#define PKR_ENC_C2_SPATIAL (PKR_ENC_C2_OH * PKR_ENC_C2_OW)
#define PKR_ENC_C2_COL_W (PKR_ENC_C2_IC * PKR_ENC_C2_K * PKR_ENC_C2_K)

#define PKR_ENC_CONV_FLAT (PKR_ENC_C2_OC * PKR_ENC_C2_SPATIAL)

#define PKR_ENC_VISITED_IN VISITED_MASK_OBS
#define PKR_ENC_VISITED_HIDDEN 8

#define PKR_SPECIES_VOCAB 256
#define PKR_SPECIES_EMBED_DIM 8
#define PKR_ITEM_VOCAB 256
#define PKR_ITEM_EMBED_DIM 4
#define PKR_LEVEL_SCALE 100.0f
#define PKR_ITEM_COUNT_SCALE 99.0f

#define PKR_BATTLE_PLAYER_SLOT PARTY_SIZE
#define PKR_BATTLE_ENEMY_SLOT (PARTY_SIZE + 1)
#define PKR_MON_SLOTS (PARTY_SIZE + 2)
#define PKR_MON_FEAT (PKR_SPECIES_EMBED_DIM + 2)

#define PKR_ENC_BATTLE_IN (BATTLE_TYPE_COUNT + 2 * PKR_MON_FEAT)
#define PKR_ENC_BATTLE_HIDDEN 24
#define PKR_ENC_PARTY_IN (PARTY_SIZE * PKR_MON_FEAT)
#define PKR_ENC_PARTY_HIDDEN 56
#define PKR_BAG_SLOT_FEAT (PKR_ITEM_EMBED_DIM + 1)
#define PKR_ENC_BAG_IN (BAG_SLOTS * PKR_BAG_SLOT_FEAT)
#define PKR_ENC_BAG_HIDDEN 32

#define PKR_ENC_CONCAT (PKR_ENC_CONV_FLAT + PKR_ENC_VISITED_HIDDEN + PKR_ENC_BATTLE_HIDDEN + \
    PKR_ENC_PARTY_HIDDEN + PKR_ENC_BAG_HIDDEN)
#define PKR_ENC_TAIL (PKR_ENC_VISITED_HIDDEN + PKR_ENC_BATTLE_HIDDEN + PKR_ENC_PARTY_HIDDEN + PKR_ENC_BAG_HIDDEN)

#define PKR_TL_VOCAB 256
#define PKR_TL_EMBED 7
#define PKR_TL_IC (PKR_TL_EMBED + 1)
#define PKR_TL_C1_OC 32
#define PKR_TL_C1_K 3
#define PKR_TL_C1_S 1
#define PKR_TL_C2_OC 64
#define PKR_TL_C2_K 3
#define PKR_TL_C2_S 2
#define PKR_TL_C1_OH ((PK_TILE_MAP_H - PKR_TL_C1_K) / PKR_TL_C1_S + 1)
#define PKR_TL_C1_OW ((PK_TILE_MAP_W - PKR_TL_C1_K) / PKR_TL_C1_S + 1)
#define PKR_TL_C1_COLW (PKR_TL_IC * PKR_TL_C1_K * PKR_TL_C1_K)
#define PKR_TL_C2_OH ((PKR_TL_C1_OH - PKR_TL_C2_K) / PKR_TL_C2_S + 1)
#define PKR_TL_C2_OW ((PKR_TL_C1_OW - PKR_TL_C2_K) / PKR_TL_C2_S + 1)
#define PKR_FXP_SCALE 4294967296.0

struct PkrConvDims {
    int ic, ih, iw;
    int oc1, k1, s1, oh1, ow1;
    int oc2, k2, s2, oh2, ow2;
    int sp1() const { return oh1 * ow1; }
    int colw1() const { return ic * k1 * k1; }
    int sp2() const { return oh2 * ow2; }
    int colw2() const { return oc1 * k2 * k2; }
    int flat() const { return oc2 * sp2(); }
};

static PkrConvDims pkr_dims(int tiles) {
    PkrConvDims d;
    if (tiles) {
        d.ic = PKR_TL_IC; d.ih = PK_TILE_MAP_H; d.iw = PK_TILE_MAP_W;
        d.oc1 = PKR_TL_C1_OC; d.k1 = PKR_TL_C1_K; d.s1 = PKR_TL_C1_S;
        d.oc2 = PKR_TL_C2_OC; d.k2 = PKR_TL_C2_K; d.s2 = PKR_TL_C2_S;
    } else {
        d.ic = PKR_ENC_C1_IC; d.ih = SCALED_HEIGHT; d.iw = SCALED_WIDTH;
        d.oc1 = PKR_ENC_C1_OC; d.k1 = PKR_ENC_C1_K; d.s1 = PKR_ENC_C1_S;
        d.oc2 = PKR_ENC_C2_OC; d.k2 = PKR_ENC_C2_K; d.s2 = PKR_ENC_C2_S;
    }
    d.oh1 = (d.ih - d.k1) / d.s1 + 1; d.ow1 = (d.iw - d.k1) / d.s1 + 1;
    d.oh2 = (d.oh1 - d.k2) / d.s2 + 1; d.ow2 = (d.ow1 - d.k2) / d.s2 + 1;
    return d;
}

__global__ void pkr_c1_im2col(
        const precision_t* __restrict__ obs, precision_t* __restrict__ col,
        int B, int obs_size) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= B * PKR_ENC_C1_SPATIAL * PKR_ENC_C1_COL_W) {
        return;
    }
    int row = idx / PKR_ENC_C1_COL_W;
    int c = idx % PKR_ENC_C1_COL_W;
    int b = row / PKR_ENC_C1_SPATIAL;
    int rem = row % PKR_ENC_C1_SPATIAL;
    int oh = rem / PKR_ENC_C1_OW;
    int ow = rem % PKR_ENC_C1_OW;
    int ic = c / (PKR_ENC_C1_K * PKR_ENC_C1_K);
    int kk = c % (PKR_ENC_C1_K * PKR_ENC_C1_K);
    int kh = kk / PKR_ENC_C1_K;
    int kw = kk % PKR_ENC_C1_K;
    int ih = oh * PKR_ENC_C1_S + kh;
    int iw = ow * PKR_ENC_C1_S + kw;
    float raw = to_float(obs[(int64_t)b * obs_size + ic * SCALED_PIXELS + ih * SCALED_WIDTH + iw]);
    col[idx] = from_float(raw / 255.0f);
}

__global__ void pkr_c2_im2col(
        const precision_t* __restrict__ input, precision_t* __restrict__ col, int B) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= B * PKR_ENC_C2_SPATIAL * PKR_ENC_C2_COL_W) {
        return;
    }
    int row = idx / PKR_ENC_C2_COL_W;
    int c = idx % PKR_ENC_C2_COL_W;
    int b = row / PKR_ENC_C2_SPATIAL;
    int rem = row % PKR_ENC_C2_SPATIAL;
    int oh = rem / PKR_ENC_C2_OW;
    int ow = rem % PKR_ENC_C2_OW;
    int ic = c / (PKR_ENC_C2_K * PKR_ENC_C2_K);
    int kk = c % (PKR_ENC_C2_K * PKR_ENC_C2_K);
    int kh = kk / PKR_ENC_C2_K;
    int kw = kk % PKR_ENC_C2_K;
    int ih = oh * PKR_ENC_C2_S + kh;
    int iw = ow * PKR_ENC_C2_S + kw;
    col[idx] = input[((int64_t)b * PKR_ENC_C2_IC + ic) * PKR_ENC_C1_SPATIAL
        + ih * PKR_ENC_C1_OW + iw];
}

__global__ void pkr_c2_col2im(
        const precision_t* __restrict__ col_grad, precision_t* __restrict__ grad_input,
        int B) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= B * PKR_ENC_C2_IC * PKR_ENC_C1_SPATIAL) {
        return;
    }
    int iw = idx % PKR_ENC_C1_OW;
    int ih = (idx / PKR_ENC_C1_OW) % PKR_ENC_C1_OH;
    int ic = (idx / PKR_ENC_C1_SPATIAL) % PKR_ENC_C2_IC;
    int b = idx / (PKR_ENC_C2_IC * PKR_ENC_C1_SPATIAL);
    float sum = 0.0f;
#pragma unroll
    for (int kh = 0; kh < PKR_ENC_C2_K; kh++) {
        int oh_num = ih - kh;
        if (oh_num < 0 || oh_num % PKR_ENC_C2_S != 0) {
            continue;
        }
        int oh = oh_num / PKR_ENC_C2_S;
        if (oh >= PKR_ENC_C2_OH) {
            continue;
        }
#pragma unroll
        for (int kw = 0; kw < PKR_ENC_C2_K; kw++) {
            int ow_num = iw - kw;
            if (ow_num < 0 || ow_num % PKR_ENC_C2_S != 0) {
                continue;
            }
            int ow = ow_num / PKR_ENC_C2_S;
            if (ow >= PKR_ENC_C2_OW) {
                continue;
            }
            int row = b * PKR_ENC_C2_SPATIAL + oh * PKR_ENC_C2_OW + ow;
            int c = ic * (PKR_ENC_C2_K * PKR_ENC_C2_K) + kh * PKR_ENC_C2_K + kw;
            sum += to_float(col_grad[(int64_t)row * PKR_ENC_C2_COL_W + c]);
        }
    }
    grad_input[idx] = from_float(sum);
}

__global__ void pkr_gather_range_kernel(
        const precision_t* __restrict__ obs, precision_t* __restrict__ out,
        int B, int obs_size, int offset, int len) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= B * len) {
        return;
    }
    int b = idx / len;
    int f = idx % len;
    out[idx] = obs[(int64_t)b * obs_size + offset + f];
}

__device__ __forceinline__ int pkr_clamp_id(float v, int vocab) {
    int i = (int)(v + 0.5f);
    i = i < 0 ? 0 : i;
    return i >= vocab ? vocab - 1 : i;
}

__device__ __forceinline__ int pkr_mon_obs_offset(int slot) {
    if (slot < PARTY_SIZE) return PARTY_OBS_OFFSET + slot * MON_FIELDS;
    return BATTLE_OBS_OFFSET + (slot == PKR_BATTLE_PLAYER_SLOT
        ? BATTLE_PLAYER_MON_OFFSET : BATTLE_ENEMY_MON_OFFSET);
}

__global__ void pkr_index_kernel(
        const precision_t* __restrict__ obs, int* __restrict__ species_idx,
        int* __restrict__ item_idx, int B, int obs_size) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= B * (PKR_MON_SLOTS + BAG_SLOTS)) {
        return;
    }
    int b = idx / (PKR_MON_SLOTS + BAG_SLOTS);
    int k = idx % (PKR_MON_SLOTS + BAG_SLOTS);
    const precision_t* row = obs + (int64_t)b * obs_size;
    if (k < PKR_MON_SLOTS) {
        species_idx[b * PKR_MON_SLOTS + k] =
            pkr_clamp_id(to_float(row[pkr_mon_obs_offset(k)]), PKR_SPECIES_VOCAB);
    } else {
        int s = k - PKR_MON_SLOTS;
        item_idx[b * BAG_SLOTS + s] =
            pkr_clamp_id(to_float(row[BAG_OBS_OFFSET + s * BAG_FIELDS]), PKR_ITEM_VOCAB);
    }
}

__device__ __forceinline__ float pkr_mon_feature(
        const precision_t* __restrict__ row, const precision_t* __restrict__ species_embed_w,
        const int* __restrict__ species_idx, int b, int slot, int f) {
    if (f < PKR_SPECIES_EMBED_DIM) {
        return to_float(species_embed_w[species_idx[b * PKR_MON_SLOTS + slot]
            * PKR_SPECIES_EMBED_DIM + f]);
    }
    int off = pkr_mon_obs_offset(slot);
    if (f == PKR_SPECIES_EMBED_DIM) return to_float(row[off + 1]) / PKR_LEVEL_SCALE;
    return to_float(row[off + 2]) / PK_OBS_HP_SCALE;
}

__global__ void pkr_party_gather_kernel(
        const precision_t* __restrict__ obs, const precision_t* __restrict__ species_embed_w,
        const int* __restrict__ species_idx, precision_t* __restrict__ out, int B, int obs_size) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= B * PKR_ENC_PARTY_IN) {
        return;
    }
    int b = idx / PKR_ENC_PARTY_IN;
    int rem = idx % PKR_ENC_PARTY_IN;
    out[idx] = from_float(pkr_mon_feature(obs + (int64_t)b * obs_size, species_embed_w,
        species_idx, b, rem / PKR_MON_FEAT, rem % PKR_MON_FEAT));
}

__global__ void pkr_battle_gather_kernel(
        const precision_t* __restrict__ obs, const precision_t* __restrict__ species_embed_w,
        const int* __restrict__ species_idx, precision_t* __restrict__ out, int B, int obs_size) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= B * PKR_ENC_BATTLE_IN) {
        return;
    }
    int b = idx / PKR_ENC_BATTLE_IN;
    int c = idx % PKR_ENC_BATTLE_IN;
    const precision_t* row = obs + (int64_t)b * obs_size;
    if (c < BATTLE_TYPE_COUNT) {
        out[idx] = from_float(pkr_clamp_id(to_float(row[BATTLE_OBS_OFFSET]), BATTLE_TYPE_COUNT) == c
            ? 1.0f : 0.0f);
        return;
    }
    c -= BATTLE_TYPE_COUNT;
    out[idx] = from_float(pkr_mon_feature(row, species_embed_w, species_idx, b,
        PKR_BATTLE_PLAYER_SLOT + c / PKR_MON_FEAT, c % PKR_MON_FEAT));
}

__global__ void pkr_bag_gather_kernel(
        const precision_t* __restrict__ obs, const precision_t* __restrict__ item_embed_w,
        const int* __restrict__ item_idx, precision_t* __restrict__ out, int B, int obs_size) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= B * PKR_ENC_BAG_IN) {
        return;
    }
    int b = idx / PKR_ENC_BAG_IN;
    int rem = idx % PKR_ENC_BAG_IN;
    int s = rem / PKR_BAG_SLOT_FEAT;
    int f = rem % PKR_BAG_SLOT_FEAT;
    if (f < PKR_ITEM_EMBED_DIM) {
        out[idx] = item_embed_w[item_idx[b * BAG_SLOTS + s] * PKR_ITEM_EMBED_DIM + f];
        return;
    }
    out[idx] = from_float(to_float(obs[(int64_t)b * obs_size + BAG_OBS_OFFSET
        + s * BAG_FIELDS + 1]) / PKR_ITEM_COUNT_SCALE);
}

__global__ void pkr_concat_kernel(
        precision_t* __restrict__ out, const precision_t* __restrict__ conv_flat,
        const precision_t* __restrict__ visited_hidden,
        const precision_t* __restrict__ battle_hidden,
        const precision_t* __restrict__ party_hidden,
        const precision_t* __restrict__ bag_hidden, int B, int flat) {
    int concat = flat + PKR_ENC_TAIL;
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= B * concat) {
        return;
    }
    int b = idx / concat;
    int c = idx % concat;
    if (c < flat) { out[idx] = conv_flat[b * flat + c]; return; }
    c -= flat;
    if (c < PKR_ENC_VISITED_HIDDEN) { out[idx] = visited_hidden[b * PKR_ENC_VISITED_HIDDEN + c]; return; }
    c -= PKR_ENC_VISITED_HIDDEN;
    if (c < PKR_ENC_BATTLE_HIDDEN) { out[idx] = battle_hidden[b * PKR_ENC_BATTLE_HIDDEN + c]; return; }
    c -= PKR_ENC_BATTLE_HIDDEN;
    if (c < PKR_ENC_PARTY_HIDDEN) { out[idx] = party_hidden[b * PKR_ENC_PARTY_HIDDEN + c]; return; }
    c -= PKR_ENC_PARTY_HIDDEN;
    out[idx] = bag_hidden[b * PKR_ENC_BAG_HIDDEN + c];
}

__global__ void pkr_split_grad_kernel(
        const precision_t* __restrict__ grad_concat, precision_t* __restrict__ conv_grad,
        precision_t* __restrict__ visited_grad, precision_t* __restrict__ battle_grad,
        precision_t* __restrict__ party_grad, precision_t* __restrict__ bag_grad, int B, int flat) {
    int concat = flat + PKR_ENC_TAIL;
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= B * concat) {
        return;
    }
    int b = idx / concat;
    int c = idx % concat;
    precision_t g = grad_concat[idx];
    if (c < flat) { conv_grad[b * flat + c] = g; return; }
    c -= flat;
    if (c < PKR_ENC_VISITED_HIDDEN) { visited_grad[b * PKR_ENC_VISITED_HIDDEN + c] = g; return; }
    c -= PKR_ENC_VISITED_HIDDEN;
    if (c < PKR_ENC_BATTLE_HIDDEN) { battle_grad[b * PKR_ENC_BATTLE_HIDDEN + c] = g; return; }
    c -= PKR_ENC_BATTLE_HIDDEN;
    if (c < PKR_ENC_PARTY_HIDDEN) { party_grad[b * PKR_ENC_PARTY_HIDDEN + c] = g; return; }
    c -= PKR_ENC_PARTY_HIDDEN;
    bag_grad[b * PKR_ENC_BAG_HIDDEN + c] = g;
}

__global__ void pkr_tl_index_kernel(
        const precision_t* __restrict__ obs, int* __restrict__ tile_idx, int B, int obs_size) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= B * PK_TILE_MAP_CELLS) {
        return;
    }
    int b = idx / PK_TILE_MAP_CELLS;
    int c = idx % PK_TILE_MAP_CELLS;
    tile_idx[idx] = pkr_clamp_id(to_float(obs[(int64_t)b * obs_size + TILE_OBS_OFFSET + c]), PKR_TL_VOCAB);
}

__global__ void pkr_tl_input_kernel(
        const precision_t* __restrict__ obs, const precision_t* __restrict__ embed_w,
        const int* __restrict__ tile_idx, precision_t* __restrict__ input, int B, int obs_size) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= B * PKR_TL_IC * PK_TILE_MAP_CELLS) {
        return;
    }
    int cell = idx % PK_TILE_MAP_CELLS;
    int ch = (idx / PK_TILE_MAP_CELLS) % PKR_TL_IC;
    int b = idx / (PKR_TL_IC * PK_TILE_MAP_CELLS);
    if (ch < PKR_TL_EMBED) {
        input[idx] = embed_w[tile_idx[b * PK_TILE_MAP_CELLS + cell] * PKR_TL_EMBED + ch];
    } else {
        input[idx] = obs[(int64_t)b * obs_size + SPRITE_OBS_OFFSET + cell];
    }
}

template <int IC, int IH, int IW, int K, int S, int OH, int OW>
__global__ void pkr_im2col_t(const precision_t* __restrict__ input, precision_t* __restrict__ col, int B) {
    constexpr int COLW = IC * K * K;
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= B * OH * OW * COLW) {
        return;
    }
    int c = idx % COLW;
    int row = idx / COLW;
    int ow = row % OW;
    int oh = (row / OW) % OH;
    int b = row / (OH * OW);
    int ic = c / (K * K);
    int kh = (c / K) % K;
    int kw = c % K;
    col[idx] = input[((b * IC + ic) * IH + oh * S + kh) * IW + ow * S + kw];
}

template <int IC, int IH, int IW, int K, int S, int OH, int OW>
__global__ void pkr_col2im_t(const precision_t* __restrict__ col, precision_t* __restrict__ grad_input, int B) {
    constexpr int COLW = IC * K * K;
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= B * IC * IH * IW) {
        return;
    }
    int iw = idx % IW;
    int ih = (idx / IW) % IH;
    int ic = (idx / (IW * IH)) % IC;
    int b = idx / (IW * IH * IC);
    float sum = 0.0f;
#pragma unroll
    for (int kh = 0; kh < K; kh++) {
        int oh_num = ih - kh;
        if (oh_num < 0 || oh_num % S != 0 || oh_num / S >= OH) {
            continue;
        }
#pragma unroll
        for (int kw = 0; kw < K; kw++) {
            int ow_num = iw - kw;
            if (ow_num < 0 || ow_num % S != 0 || ow_num / S >= OW) {
                continue;
            }
            int row = (b * OH + oh_num / S) * OW + ow_num / S;
            sum += to_float(col[row * COLW + ic * K * K + kh * K + kw]);
        }
    }
    grad_input[idx] = from_float(sum);
}

__device__ __forceinline__ unsigned long long pkr_fxp(float g) {
    return (unsigned long long)__double2ll_rn((double)g * PKR_FXP_SCALE);
}

template <int V, int E, typename GradAt>
__global__ void pkr_embed_hist_kernel(long long* __restrict__ wgrad_i, const int* __restrict__ idx, int n,
        GradAt grad) {
    __shared__ long long acc[V * E];
    for (int i = threadIdx.x; i < V * E; i += blockDim.x) {
        acc[i] = 0;
    }
    __syncthreads();
    int lane = threadIdx.x & 31;
    int warp_base = (blockIdx.x * blockDim.x + threadIdx.x) - lane;
    int stride = blockDim.x * gridDim.x;
    for (int base = warp_base; base < n; base += stride) {
        int pos = base + lane;
        int v = pos < n ? idx[pos] : -1;
        unsigned peers = __match_any_sync(0xffffffffu, v);
        bool leader = lane == __ffs(peers) - 1;
        float g[E];
        if (v >= 0) {
            grad(pos, g);
        }
#pragma unroll
        for (int e = 0; e < E; e++) {
            long long mine = v >= 0 ? (long long)pkr_fxp(g[e]) : 0;
            long long sum = 0;
            for (unsigned m = peers; m; m &= m - 1) {
                sum += __shfl_sync(peers, mine, __ffs(m) - 1);
            }
            if (leader && v >= 0 && sum != 0) {
                atomicAdd((unsigned long long*)&acc[v * E + e], (unsigned long long)sum);
            }
        }
    }
    __syncthreads();
    for (int i = threadIdx.x; i < V * E; i += blockDim.x) {
        if (acc[i] != 0) {
            atomicAdd((unsigned long long*)&wgrad_i[i], (unsigned long long)acc[i]);
        }
    }
}

struct PkrSpeciesGrad {
    const precision_t* party;
    const precision_t* battle;
    __device__ void operator()(int pos, float* g) const {
        int b = pos / PKR_MON_SLOTS;
        int s = pos % PKR_MON_SLOTS;
        const precision_t* src = s < PARTY_SIZE ? party + (b * PARTY_SIZE + s) * PKR_MON_FEAT
            : battle + b * PKR_ENC_BATTLE_IN + BATTLE_TYPE_COUNT + (s - PARTY_SIZE) * PKR_MON_FEAT;
#pragma unroll
        for (int e = 0; e < PKR_SPECIES_EMBED_DIM; e++) {
            g[e] = to_float(src[e]);
        }
    }
};

struct PkrItemGrad {
    const precision_t* bag;
    __device__ void operator()(int pos, float* g) const {
#pragma unroll
        for (int e = 0; e < PKR_ITEM_EMBED_DIM; e++) {
            g[e] = to_float(bag[pos * PKR_BAG_SLOT_FEAT + e]);
        }
    }
};

#define PKR_TL_DX_STRIDE 8

__global__ void pkr_tl_input_grad_kernel(
        const precision_t* __restrict__ conv1_grad, const precision_t* __restrict__ conv1_w,
        float* __restrict__ dx, int B) {
    constexpr int TAPS = PKR_TL_C1_K * PKR_TL_C1_K;
    __shared__ float4 w[TAPS * PKR_TL_C1_OC * 2];
    float* wf = (float*)w;
    for (int i = threadIdx.x; i < TAPS * PKR_TL_C1_OC * 8; i += blockDim.x) {
        int e = i % 8, oc = (i / 8) % PKR_TL_C1_OC, tap = i / (8 * PKR_TL_C1_OC);
        wf[i] = e < PKR_TL_EMBED ? to_float(conv1_w[oc * PKR_TL_C1_COLW + e * TAPS + tap]) : 0.0f;
    }
    __syncthreads();
    int pos = blockIdx.x * blockDim.x + threadIdx.x;
    if (pos >= B * PK_TILE_MAP_CELLS) {
        return;
    }
    int b = pos / PK_TILE_MAP_CELLS;
    int cell = pos % PK_TILE_MAP_CELLS;
    int ih = cell / PK_TILE_MAP_W;
    int iw = cell % PK_TILE_MAP_W;
    float4 lo = make_float4(0.0f, 0.0f, 0.0f, 0.0f), hi = lo;
    const precision_t* dy = conv1_grad + (int64_t)b * PKR_TL_C1_OC * PKR_TL_C1_OH * PKR_TL_C1_OW;
#pragma unroll
    for (int kh = 0; kh < PKR_TL_C1_K; kh++) {
        int oh = ih - kh;
        if (oh < 0 || oh >= PKR_TL_C1_OH) {
            continue;
        }
#pragma unroll
        for (int kw = 0; kw < PKR_TL_C1_K; kw++) {
            int ow = iw - kw;
            if (ow < 0 || ow >= PKR_TL_C1_OW) {
                continue;
            }
            const float4* wt = w + (kh * PKR_TL_C1_K + kw) * PKR_TL_C1_OC * 2;
            const precision_t* dyp = dy + oh * PKR_TL_C1_OW + ow;
#pragma unroll 8
            for (int oc = 0; oc < PKR_TL_C1_OC; oc++) {
                float d = to_float(dyp[oc * PKR_TL_C1_OH * PKR_TL_C1_OW]);
                float4 a = wt[oc * 2], c = wt[oc * 2 + 1];
                lo.x += d * a.x; lo.y += d * a.y; lo.z += d * a.z; lo.w += d * a.w;
                hi.x += d * c.x; hi.y += d * c.y; hi.z += d * c.z;
            }
        }
    }
    float4* out = (float4*)(dx + (int64_t)pos * PKR_TL_DX_STRIDE);
    out[0] = lo;
    out[1] = hi;
}

struct PkrTileGrad {
    const float* dx;
    __device__ void operator()(int pos, float* g) const {
#pragma unroll
        for (int e = 0; e < PKR_TL_EMBED; e++) {
            g[e] = dx[(int64_t)pos * PKR_TL_DX_STRIDE + e];
        }
    }
};

__global__ void pkr_fxp_to_precision_kernel(
        precision_t* __restrict__ dst, const long long* __restrict__ src, int n) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx < n) {
        dst[idx] = from_float((float)((double)src[idx] * (1.0 / PKR_FXP_SCALE)));
    }
}

template <int V, int E, typename GradAt>
static void pkr_embed_wgrad(Long* scratch, Prec* wgrad, const int* idx, int n, GradAt grad, cudaStream_t stream) {
    cudaMemsetAsync(scratch->data, 0, V * E * sizeof(long long), stream);
    int blocks = (n + BLOCK_SIZE - 1) / BLOCK_SIZE;
    blocks = blocks < 1 ? 1 : blocks > 512 ? 512 : blocks;
    pkr_embed_hist_kernel<V, E, GradAt><<<blocks, BLOCK_SIZE, 0, stream>>>(
        (long long*)scratch->data, idx, n, grad);
    pkr_fxp_to_precision_kernel<<<grid_size(V * E), BLOCK_SIZE, 0, stream>>>(
        wgrad->data, (long long*)scratch->data, V * E);
}

__global__ void pkr_transpose_kernel(
        const precision_t* __restrict__ src, precision_t* __restrict__ dst, int R, int C, int relu) {
    __shared__ precision_t tile[32][33];
    const precision_t* sb = src + (int64_t)blockIdx.z * R * C;
    precision_t* db = dst + (int64_t)blockIdx.z * R * C;
    int x = blockIdx.x * 32 + threadIdx.x;
    int y = blockIdx.y * 32 + threadIdx.y;
    for (int j = 0; j < 32; j += 8) {
        if (x < C && y + j < R) {
            tile[threadIdx.y + j][threadIdx.x] = sb[(y + j) * C + x];
        }
    }
    __syncthreads();
    x = blockIdx.y * 32 + threadIdx.x;
    y = blockIdx.x * 32 + threadIdx.y;
    for (int j = 0; j < 32; j += 8) {
        if (x < R && y + j < C) {
            precision_t v = tile[threadIdx.x][threadIdx.y + j];
            db[(y + j) * R + x] = relu ? from_float(fmaxf(0.0f, to_float(v))) : v;
        }
    }
}

static void pkr_rows_to_nchw_launch(const precision_t* src, precision_t* dst, int B, int OC, int spatial,
        int relu, cudaStream_t stream) {
    dim3 grid((OC + 31) / 32, (spatial + 31) / 32, B);
    pkr_transpose_kernel<<<grid, dim3(32, 8), 0, stream>>>(src, dst, spatial, OC, relu);
}

static void pkr_nchw_to_rows_launch(const precision_t* src, precision_t* dst, int B, int OC, int spatial,
        cudaStream_t stream) {
    dim3 grid((spatial + 31) / 32, (OC + 31) / 32, B);
    pkr_transpose_kernel<<<grid, dim3(32, 8), 0, stream>>>(src, dst, OC, spatial, 0);
}

__global__ void pkr_relu_kernel(precision_t* __restrict__ data, int total) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= total) {
        return;
    }
    data[idx] = from_float(fmaxf(0.0f, to_float(data[idx])));
}

__global__ void pkr_relu_backward_kernel(
        precision_t* __restrict__ grad, const precision_t* __restrict__ out, int total) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= total) {
        return;
    }
    if (to_float(out[idx]) <= 0.0f) {
        grad[idx] = from_float(0.0f);
    }
}

struct PokeredEncoderWeights {
    Prec conv1_w, conv2_w;
    Prec visited_w, battle_w, party_w, bag_w;
    Prec species_embed_w, item_embed_w;
    Prec proj_w;
    Prec tile_embed_w;
    int obs_size, hidden;
    int tiles, concat;
    PkrConvDims d;
};

struct PokeredEncoderActivations {
    Prec conv1_out, conv1_grad, conv1_wgrad, col1, mm1;
    Prec conv2_out, conv2_grad, conv2_wgrad, col2, mm2;
    Prec visited_in, visited_out, visited_grad, visited_wgrad;
    Prec battle_in, battle_out, battle_grad, battle_wgrad;
    Prec party_in, party_out, party_grad, party_wgrad;
    Prec bag_in, bag_out, bag_grad, bag_wgrad;
    Int species_idx, item_idx;
    Prec species_embed_wgrad, item_embed_wgrad;
    Prec concat, out, proj_wgrad;
    Prec tl_in, tile_embed_wgrad;
    Int tile_idx;
    Long tile_embed_wgrad_i, species_wgrad_i, item_wgrad_i;
    Float tile_dx;
};

static void pkr_conv_wgrad(
        precision_t* grad_output_nchw, precision_t* wgrad,
        precision_t* col_buf, precision_t* mm_buf,
        int B, int OC, int spatial, int col_cols, cudaStream_t stream) {
    int col_rows = B * spatial;
    pkr_nchw_to_rows_launch(grad_output_nchw, mm_buf, B, OC, spatial, stream);
    Prec mm_t = {.data = mm_buf, .shape = {col_rows, OC}};
    Prec col_t = {.data = col_buf, .shape = {col_rows, col_cols}};
    Prec wg_t = {.data = wgrad, .shape = {OC, col_cols}};
    puf_mm_tn(&mm_t, &col_t, &wg_t, stream);
}

static void pkr_branch_forward(
        Prec* branch_in, Prec* branch_w, Prec* branch_out, int hidden, int B, cudaStream_t stream) {
    puf_mm(branch_in, branch_w, branch_out, stream);
    pkr_relu_kernel<<<grid_size(B * hidden), BLOCK_SIZE, 0, stream>>>(branch_out->data, B * hidden);
}

static void pkr_branch_backward(
        Prec* branch_grad, Prec* branch_out, Prec* branch_in, Prec* branch_wgrad,
        Prec* branch_w, int hidden, cudaStream_t stream) {
    pkr_relu_backward_kernel<<<grid_size(branch_grad->shape[0] * hidden), BLOCK_SIZE, 0, stream>>>(
        branch_grad->data, branch_out->data, branch_grad->shape[0] * hidden);
    puf_mm_tn(branch_grad, branch_in, branch_wgrad, stream);
    if (branch_w) {
        puf_mm_nn(branch_grad, branch_w, branch_in, stream);
    }
}

static void pkr_tiles_conv_forward(PokeredEncoderWeights* ew, PokeredEncoderActivations* a,
        Prec input, int B, cudaStream_t stream) {
    const PkrConvDims& d = ew->d;
    pkr_tl_index_kernel<<<grid_size(B * PK_TILE_MAP_CELLS), BLOCK_SIZE, 0, stream>>>(
        input.data, a->tile_idx.data, B, ew->obs_size);
    pkr_tl_input_kernel<<<grid_size(B * PKR_TL_IC * PK_TILE_MAP_CELLS), BLOCK_SIZE, 0, stream>>>(
        input.data, ew->tile_embed_w.data, a->tile_idx.data, a->tl_in.data, B, ew->obs_size);
    int r1 = B * d.sp1();
    pkr_im2col_t<PKR_TL_IC, PK_TILE_MAP_H, PK_TILE_MAP_W, PKR_TL_C1_K, PKR_TL_C1_S, PKR_TL_C1_OH, PKR_TL_C1_OW>
        <<<grid_size(r1 * d.colw1()), BLOCK_SIZE, 0, stream>>>(a->tl_in.data, a->col1.data, B);
    Prec col1 = {.data = a->col1.data, .shape = {r1, d.colw1()}};
    Prec mm1 = {.data = a->mm1.data, .shape = {r1, d.oc1}};
    puf_mm(&col1, &ew->conv1_w, &mm1, stream);
    pkr_rows_to_nchw_launch(a->mm1.data, a->conv1_out.data, B, d.oc1, d.sp1(), 1, stream);
    int r2 = B * d.sp2();
    pkr_im2col_t<PKR_TL_C1_OC, PKR_TL_C1_OH, PKR_TL_C1_OW, PKR_TL_C2_K, PKR_TL_C2_S, PKR_TL_C2_OH, PKR_TL_C2_OW>
        <<<grid_size(r2 * d.colw2()), BLOCK_SIZE, 0, stream>>>(a->conv1_out.data, a->col2.data, B);
    Prec col2 = {.data = a->col2.data, .shape = {r2, d.colw2()}};
    Prec mm2 = {.data = a->mm2.data, .shape = {r2, d.oc2}};
    puf_mm(&col2, &ew->conv2_w, &mm2, stream);
    pkr_rows_to_nchw_launch(a->mm2.data, a->conv2_out.data, B, d.oc2, d.sp2(), 0, stream);
}

static void pkr_tiles_conv_backward(PokeredEncoderWeights* ew, PokeredEncoderActivations* a,
        int B, cudaStream_t stream) {
    const PkrConvDims& d = ew->d;
    pkr_conv_wgrad(a->conv2_grad.data, a->conv2_wgrad.data, a->col2.data, a->mm2.data,
        B, d.oc2, d.sp2(), d.colw2(), stream);
    Prec mm2 = {.data = a->mm2.data, .shape = {B * d.sp2(), d.oc2}};
    Prec col2 = {.data = a->col2.data, .shape = {B * d.sp2(), d.colw2()}};
    puf_mm_nn(&mm2, &ew->conv2_w, &col2, stream);
    pkr_col2im_t<PKR_TL_C1_OC, PKR_TL_C1_OH, PKR_TL_C1_OW, PKR_TL_C2_K, PKR_TL_C2_S, PKR_TL_C2_OH, PKR_TL_C2_OW>
        <<<grid_size(B * d.oc1 * d.sp1()), BLOCK_SIZE, 0, stream>>>(a->col2.data, a->conv1_grad.data, B);
    pkr_relu_backward_kernel<<<grid_size(B * d.oc1 * d.sp1()), BLOCK_SIZE, 0, stream>>>(
        a->conv1_grad.data, a->conv1_out.data, B * d.oc1 * d.sp1());
    pkr_conv_wgrad(a->conv1_grad.data, a->conv1_wgrad.data, a->col1.data, a->mm1.data,
        B, d.oc1, d.sp1(), d.colw1(), stream);
    pkr_tl_input_grad_kernel<<<grid_size(B * PK_TILE_MAP_CELLS), BLOCK_SIZE, 0, stream>>>(
        a->conv1_grad.data, ew->conv1_w.data, a->tile_dx.data, B);
    pkr_embed_wgrad<PKR_TL_VOCAB, PKR_TL_EMBED>(&a->tile_embed_wgrad_i, &a->tile_embed_wgrad,
        a->tile_idx.data, B * PK_TILE_MAP_CELLS, PkrTileGrad{a->tile_dx.data}, stream);
}

static Prec pokered_encoder_forward(
        void* w, void* activations, Prec input, cudaStream_t stream) {
    PokeredEncoderWeights* ew = (PokeredEncoderWeights*)w;
    PokeredEncoderActivations* a = (PokeredEncoderActivations*)activations;
    int B = input.shape[0];

    if (ew->tiles) {
        pkr_tiles_conv_forward(ew, a, input, B, stream);
    } else {
        int c1_rows = B * PKR_ENC_C1_SPATIAL;
        pkr_c1_im2col<<<grid_size(c1_rows * PKR_ENC_C1_COL_W), BLOCK_SIZE, 0, stream>>>(
            input.data, a->col1.data, B, ew->obs_size);
        Prec c1_col = {.data = a->col1.data, .shape = {c1_rows, PKR_ENC_C1_COL_W}};
        Prec c1_mm = {.data = a->mm1.data, .shape = {c1_rows, PKR_ENC_C1_OC}};
        puf_mm(&c1_col, &ew->conv1_w, &c1_mm, stream);
        pkr_rows_to_nchw_launch(a->mm1.data, a->conv1_out.data, B, PKR_ENC_C1_OC, PKR_ENC_C1_SPATIAL, 1, stream);

        int c2_rows = B * PKR_ENC_C2_SPATIAL;
        pkr_c2_im2col<<<grid_size(c2_rows * PKR_ENC_C2_COL_W), BLOCK_SIZE, 0, stream>>>(
            a->conv1_out.data, a->col2.data, B);
        Prec c2_col = {.data = a->col2.data, .shape = {c2_rows, PKR_ENC_C2_COL_W}};
        Prec c2_mm = {.data = a->mm2.data, .shape = {c2_rows, PKR_ENC_C2_OC}};
        puf_mm(&c2_col, &ew->conv2_w, &c2_mm, stream);
        pkr_rows_to_nchw_launch(a->mm2.data, a->conv2_out.data, B, PKR_ENC_C2_OC, PKR_ENC_C2_SPATIAL, 0, stream);
    }

    pkr_gather_range_kernel<<<grid_size(B * PKR_ENC_VISITED_IN), BLOCK_SIZE, 0, stream>>>(
        input.data, a->visited_in.data, B, ew->obs_size, VISITED_MASK_OFFSET, PKR_ENC_VISITED_IN);
    pkr_branch_forward(&a->visited_in, &ew->visited_w, &a->visited_out,
        PKR_ENC_VISITED_HIDDEN, B, stream);

    pkr_index_kernel<<<grid_size(B * (PKR_MON_SLOTS + BAG_SLOTS)), BLOCK_SIZE, 0, stream>>>(
        input.data, a->species_idx.data, a->item_idx.data, B, ew->obs_size);

    pkr_battle_gather_kernel<<<grid_size(B * PKR_ENC_BATTLE_IN), BLOCK_SIZE, 0, stream>>>(
        input.data, ew->species_embed_w.data, a->species_idx.data, a->battle_in.data, B, ew->obs_size);
    pkr_branch_forward(&a->battle_in, &ew->battle_w, &a->battle_out, PKR_ENC_BATTLE_HIDDEN, B, stream);

    pkr_party_gather_kernel<<<grid_size(B * PKR_ENC_PARTY_IN), BLOCK_SIZE, 0, stream>>>(
        input.data, ew->species_embed_w.data, a->species_idx.data, a->party_in.data, B, ew->obs_size);
    pkr_branch_forward(&a->party_in, &ew->party_w, &a->party_out, PKR_ENC_PARTY_HIDDEN, B, stream);

    pkr_bag_gather_kernel<<<grid_size(B * PKR_ENC_BAG_IN), BLOCK_SIZE, 0, stream>>>(
        input.data, ew->item_embed_w.data, a->item_idx.data, a->bag_in.data, B, ew->obs_size);
    pkr_branch_forward(&a->bag_in, &ew->bag_w, &a->bag_out, PKR_ENC_BAG_HIDDEN, B, stream);

    pkr_concat_kernel<<<grid_size(B * ew->concat), BLOCK_SIZE, 0, stream>>>(
        a->concat.data, a->conv2_out.data, a->visited_out.data, a->battle_out.data,
        a->party_out.data, a->bag_out.data, B, ew->d.flat());

    puf_mm(&a->concat, &ew->proj_w, &a->out, stream);
    pkr_relu_kernel<<<grid_size(B * ew->hidden), BLOCK_SIZE, 0, stream>>>(
        a->out.data, B * ew->hidden);
    return a->out;
}

static void pokered_encoder_backward(
        void* w, void* activations, Prec grad, cudaStream_t stream) {
    PokeredEncoderWeights* ew = (PokeredEncoderWeights*)w;
    PokeredEncoderActivations* a = (PokeredEncoderActivations*)activations;
    int B = grad.shape[0];
    int H = ew->hidden;

    pkr_relu_backward_kernel<<<grid_size(B * H), BLOCK_SIZE, 0, stream>>>(
        grad.data, a->out.data, B * H);
    puf_mm_tn(&grad, &a->concat, &a->proj_wgrad, stream);
    Prec grad_concat = {.data = a->concat.data, .shape = {B, ew->concat}};
    puf_mm_nn(&grad, &ew->proj_w, &grad_concat, stream);

    pkr_split_grad_kernel<<<grid_size(B * ew->concat), BLOCK_SIZE, 0, stream>>>(
        grad_concat.data, a->conv2_grad.data, a->visited_grad.data, a->battle_grad.data,
        a->party_grad.data, a->bag_grad.data, B, ew->d.flat());

    pkr_branch_backward(&a->visited_grad, &a->visited_out, &a->visited_in, &a->visited_wgrad,
        NULL, PKR_ENC_VISITED_HIDDEN, stream);
    pkr_branch_backward(&a->battle_grad, &a->battle_out, &a->battle_in, &a->battle_wgrad,
        &ew->battle_w, PKR_ENC_BATTLE_HIDDEN, stream);
    pkr_branch_backward(&a->party_grad, &a->party_out, &a->party_in, &a->party_wgrad,
        &ew->party_w, PKR_ENC_PARTY_HIDDEN, stream);
    pkr_branch_backward(&a->bag_grad, &a->bag_out, &a->bag_in, &a->bag_wgrad,
        &ew->bag_w, PKR_ENC_BAG_HIDDEN, stream);

    pkr_embed_wgrad<PKR_SPECIES_VOCAB, PKR_SPECIES_EMBED_DIM>(&a->species_wgrad_i, &a->species_embed_wgrad,
        a->species_idx.data, B * PKR_MON_SLOTS, PkrSpeciesGrad{a->party_in.data, a->battle_in.data}, stream);
    pkr_embed_wgrad<PKR_ITEM_VOCAB, PKR_ITEM_EMBED_DIM>(&a->item_wgrad_i, &a->item_embed_wgrad,
        a->item_idx.data, B * BAG_SLOTS, PkrItemGrad{a->bag_in.data}, stream);

    if (ew->tiles) {
        pkr_tiles_conv_backward(ew, a, B, stream);
        return;
    }
    pkr_conv_wgrad(a->conv2_grad.data, a->conv2_wgrad.data,
        a->col2.data, a->mm2.data, B, PKR_ENC_C2_OC, PKR_ENC_C2_SPATIAL,
        PKR_ENC_C2_COL_W, stream);
    Prec mm2 = {.data = a->mm2.data, .shape = {B * PKR_ENC_C2_SPATIAL, PKR_ENC_C2_OC}};
    Prec col2 = {.data = a->col2.data, .shape = {B * PKR_ENC_C2_SPATIAL, PKR_ENC_C2_COL_W}};
    puf_mm_nn(&mm2, &ew->conv2_w, &col2, stream);
    pkr_c2_col2im<<<grid_size(B * PKR_ENC_C2_IC * PKR_ENC_C1_SPATIAL), BLOCK_SIZE, 0,
        stream>>>(a->col2.data, a->conv1_grad.data, B);

    pkr_relu_backward_kernel<<<grid_size(B * PKR_ENC_C1_OC * PKR_ENC_C1_SPATIAL),
        BLOCK_SIZE, 0, stream>>>(
        a->conv1_grad.data, a->conv1_out.data, B * PKR_ENC_C1_OC * PKR_ENC_C1_SPATIAL);
    pkr_conv_wgrad(a->conv1_grad.data, a->conv1_wgrad.data,
        a->col1.data, a->mm1.data, B, PKR_ENC_C1_OC, PKR_ENC_C1_SPATIAL,
        PKR_ENC_C1_COL_W, stream);
}

static void pokered_encoder_init_weights(
        void* w, uint64_t* seed, cudaStream_t stream) {
    PokeredEncoderWeights* ew = (PokeredEncoderWeights*)w;
    Prec c1 = {.data = ew->conv1_w.data, .shape = {ew->d.oc1, ew->d.colw1()}};
    Prec c2 = {.data = ew->conv2_w.data, .shape = {ew->d.oc2, ew->d.colw2()}};
    Prec vis_w = {.data = ew->visited_w.data, .shape = {PKR_ENC_VISITED_HIDDEN, PKR_ENC_VISITED_IN}};
    Prec bat_w = {.data = ew->battle_w.data, .shape = {PKR_ENC_BATTLE_HIDDEN, PKR_ENC_BATTLE_IN}};
    Prec pty_w = {.data = ew->party_w.data, .shape = {PKR_ENC_PARTY_HIDDEN, PKR_ENC_PARTY_IN}};
    Prec bag_w = {.data = ew->bag_w.data, .shape = {PKR_ENC_BAG_HIDDEN, PKR_ENC_BAG_IN}};
    Prec proj = {.data = ew->proj_w.data, .shape = {ew->hidden, ew->concat}};
    puf_kaiming_init(&c1, sqrtf(2.0f), (*seed)++, stream);
    puf_kaiming_init(&c2, sqrtf(2.0f), (*seed)++, stream);
    puf_kaiming_init(&vis_w, sqrtf(2.0f), (*seed)++, stream);
    puf_kaiming_init(&bat_w, sqrtf(2.0f), (*seed)++, stream);
    puf_kaiming_init(&pty_w, sqrtf(2.0f), (*seed)++, stream);
    puf_kaiming_init(&bag_w, sqrtf(2.0f), (*seed)++, stream);
    puf_normal_init(&ew->species_embed_w, 0.02f, (*seed)++, stream);
    puf_normal_init(&ew->item_embed_w, 0.02f, (*seed)++, stream);
    puf_kaiming_init(&proj, sqrtf(2.0f), (*seed)++, stream);
    if (ew->tiles) {
        puf_normal_init(&ew->tile_embed_w, 0.02f, (*seed)++, stream);
    }
}

static void pokered_encoder_reg_params(void* w, Allocator* alloc) {
    PokeredEncoderWeights* ew = (PokeredEncoderWeights*)w;
    ew->conv1_w = {.shape = {ew->d.oc1, ew->d.colw1()}};
    ew->conv2_w = {.shape = {ew->d.oc2, ew->d.colw2()}};
    ew->visited_w = {.shape = {PKR_ENC_VISITED_HIDDEN, PKR_ENC_VISITED_IN}};
    ew->battle_w = {.shape = {PKR_ENC_BATTLE_HIDDEN, PKR_ENC_BATTLE_IN}};
    ew->party_w = {.shape = {PKR_ENC_PARTY_HIDDEN, PKR_ENC_PARTY_IN}};
    ew->bag_w = {.shape = {PKR_ENC_BAG_HIDDEN, PKR_ENC_BAG_IN}};
    ew->species_embed_w = {.shape = {PKR_SPECIES_VOCAB, PKR_SPECIES_EMBED_DIM}};
    ew->item_embed_w = {.shape = {PKR_ITEM_VOCAB, PKR_ITEM_EMBED_DIM}};
    ew->proj_w = {.shape = {ew->hidden, ew->concat}};
    alloc_register(alloc, &ew->conv1_w);
    alloc_register(alloc, &ew->conv2_w);
    alloc_register(alloc, &ew->visited_w);
    alloc_register(alloc, &ew->battle_w);
    alloc_register(alloc, &ew->party_w);
    alloc_register(alloc, &ew->bag_w);
    alloc_register(alloc, &ew->species_embed_w);
    alloc_register(alloc, &ew->item_embed_w);
    alloc_register(alloc, &ew->proj_w);
    if (ew->tiles) {
        ew->tile_embed_w = {.shape = {PKR_TL_VOCAB, PKR_TL_EMBED}};
        alloc_register(alloc, &ew->tile_embed_w);
    }
}

static void pokered_encoder_reg_train(
        void* w, void* activations, Allocator* acts, Allocator* grads, int B_TT) {
    PokeredEncoderWeights* ew = (PokeredEncoderWeights*)w;
    PokeredEncoderActivations* a = (PokeredEncoderActivations*)activations;
    const PkrConvDims& d = ew->d;
    a->conv1_out = {.shape = {B_TT * d.oc1 * d.sp1()}};
    a->conv1_grad = {.shape = {B_TT * d.oc1 * d.sp1()}};
    a->conv1_wgrad = {.shape = {d.oc1, d.colw1()}};
    a->col1 = {.shape = {B_TT * d.sp1(), d.colw1()}};
    a->mm1 = {.shape = {B_TT * d.sp1(), d.oc1}};
    a->conv2_out = {.shape = {B_TT * d.oc2 * d.sp2()}};
    a->conv2_grad = {.shape = {B_TT * d.oc2 * d.sp2()}};
    a->conv2_wgrad = {.shape = {d.oc2, d.colw2()}};
    a->col2 = {.shape = {B_TT * d.sp2(), d.colw2()}};
    a->mm2 = {.shape = {B_TT * d.sp2(), d.oc2}};

    a->visited_in = {.shape = {B_TT, PKR_ENC_VISITED_IN}};
    a->visited_out = {.shape = {B_TT, PKR_ENC_VISITED_HIDDEN}};
    a->visited_grad = {.shape = {B_TT, PKR_ENC_VISITED_HIDDEN}};
    a->visited_wgrad = {.shape = {PKR_ENC_VISITED_HIDDEN, PKR_ENC_VISITED_IN}};
    a->battle_in = {.shape = {B_TT, PKR_ENC_BATTLE_IN}};
    a->battle_out = {.shape = {B_TT, PKR_ENC_BATTLE_HIDDEN}};
    a->battle_grad = {.shape = {B_TT, PKR_ENC_BATTLE_HIDDEN}};
    a->battle_wgrad = {.shape = {PKR_ENC_BATTLE_HIDDEN, PKR_ENC_BATTLE_IN}};
    a->party_in = {.shape = {B_TT, PKR_ENC_PARTY_IN}};
    a->party_out = {.shape = {B_TT, PKR_ENC_PARTY_HIDDEN}};
    a->party_grad = {.shape = {B_TT, PKR_ENC_PARTY_HIDDEN}};
    a->party_wgrad = {.shape = {PKR_ENC_PARTY_HIDDEN, PKR_ENC_PARTY_IN}};
    a->bag_in = {.shape = {B_TT, PKR_ENC_BAG_IN}};
    a->bag_out = {.shape = {B_TT, PKR_ENC_BAG_HIDDEN}};
    a->bag_grad = {.shape = {B_TT, PKR_ENC_BAG_HIDDEN}};
    a->bag_wgrad = {.shape = {PKR_ENC_BAG_HIDDEN, PKR_ENC_BAG_IN}};

    a->species_idx = {.shape = {B_TT, PKR_MON_SLOTS}};
    a->item_idx = {.shape = {B_TT, BAG_SLOTS}};
    a->species_embed_wgrad = {.shape = {PKR_SPECIES_VOCAB, PKR_SPECIES_EMBED_DIM}};
    a->item_embed_wgrad = {.shape = {PKR_ITEM_VOCAB, PKR_ITEM_EMBED_DIM}};

    a->concat = {.shape = {B_TT, ew->concat}};
    a->out = {.shape = {B_TT, ew->hidden}};
    a->proj_wgrad = {.shape = {ew->hidden, ew->concat}};

    alloc_register(acts, &a->conv1_out); alloc_register(acts, &a->conv1_grad);
    alloc_register(grads, &a->conv1_wgrad);
    alloc_register(acts, &a->col1); alloc_register(acts, &a->mm1);
    alloc_register(acts, &a->conv2_out); alloc_register(acts, &a->conv2_grad);
    alloc_register(grads, &a->conv2_wgrad);
    alloc_register(acts, &a->col2); alloc_register(acts, &a->mm2);

    alloc_register(acts, &a->visited_in); alloc_register(acts, &a->visited_out);
    alloc_register(acts, &a->visited_grad); alloc_register(grads, &a->visited_wgrad);
    alloc_register(acts, &a->battle_in); alloc_register(acts, &a->battle_out);
    alloc_register(acts, &a->battle_grad); alloc_register(grads, &a->battle_wgrad);
    alloc_register(acts, &a->party_in); alloc_register(acts, &a->party_out);
    alloc_register(acts, &a->party_grad); alloc_register(grads, &a->party_wgrad);
    alloc_register(acts, &a->bag_in); alloc_register(acts, &a->bag_out);
    alloc_register(acts, &a->bag_grad); alloc_register(grads, &a->bag_wgrad);

    alloc_register(acts, &a->species_idx); alloc_register(acts, &a->item_idx);
    a->species_wgrad_i = {.shape = {PKR_SPECIES_VOCAB * PKR_SPECIES_EMBED_DIM}};
    a->item_wgrad_i = {.shape = {PKR_ITEM_VOCAB * PKR_ITEM_EMBED_DIM}};
    alloc_register(acts, &a->species_wgrad_i);
    alloc_register(acts, &a->item_wgrad_i);
    alloc_register(grads, &a->species_embed_wgrad);
    alloc_register(grads, &a->item_embed_wgrad);

    alloc_register(acts, &a->concat);
    alloc_register(acts, &a->out);
    alloc_register(grads, &a->proj_wgrad);
    if (ew->tiles) {
        a->tl_in = {.shape = {B_TT * PKR_TL_IC * PK_TILE_MAP_CELLS}};
        a->tile_idx = {.shape = {B_TT, PK_TILE_MAP_CELLS}};
        a->tile_embed_wgrad_i = {.shape = {PKR_TL_VOCAB * PKR_TL_EMBED}};
        a->tile_embed_wgrad = {.shape = {PKR_TL_VOCAB, PKR_TL_EMBED}};
        a->tile_dx = {.shape = {B_TT * PK_TILE_MAP_CELLS * PKR_TL_DX_STRIDE}};
        alloc_register(acts, &a->tile_dx);
        alloc_register(acts, &a->tl_in);
        alloc_register(acts, &a->tile_idx);
        alloc_register(acts, &a->tile_embed_wgrad_i);
        alloc_register(grads, &a->tile_embed_wgrad);
    }
}

static void pokered_encoder_reg_rollout(
        void* w, void* activations, Allocator* alloc, int B) {
    PokeredEncoderWeights* ew = (PokeredEncoderWeights*)w;
    PokeredEncoderActivations* a = (PokeredEncoderActivations*)activations;
    const PkrConvDims& d = ew->d;
    a->conv1_out = {.shape = {B * d.oc1 * d.sp1()}};
    a->col1 = {.shape = {B * d.sp1(), d.colw1()}};
    a->mm1 = {.shape = {B * d.sp1(), d.oc1}};
    a->conv2_out = {.shape = {B * d.oc2 * d.sp2()}};
    a->col2 = {.shape = {B * d.sp2(), d.colw2()}};
    a->mm2 = {.shape = {B * d.sp2(), d.oc2}};

    a->visited_in = {.shape = {B, PKR_ENC_VISITED_IN}};
    a->visited_out = {.shape = {B, PKR_ENC_VISITED_HIDDEN}};
    a->battle_in = {.shape = {B, PKR_ENC_BATTLE_IN}};
    a->battle_out = {.shape = {B, PKR_ENC_BATTLE_HIDDEN}};
    a->party_in = {.shape = {B, PKR_ENC_PARTY_IN}};
    a->party_out = {.shape = {B, PKR_ENC_PARTY_HIDDEN}};
    a->bag_in = {.shape = {B, PKR_ENC_BAG_IN}};
    a->bag_out = {.shape = {B, PKR_ENC_BAG_HIDDEN}};

    a->species_idx = {.shape = {B, PKR_MON_SLOTS}};
    a->item_idx = {.shape = {B, BAG_SLOTS}};

    a->concat = {.shape = {B, ew->concat}};
    a->out = {.shape = {B, ew->hidden}};

    alloc_register(alloc, &a->conv1_out);
    alloc_register(alloc, &a->col1); alloc_register(alloc, &a->mm1);
    alloc_register(alloc, &a->conv2_out);
    alloc_register(alloc, &a->col2); alloc_register(alloc, &a->mm2);
    alloc_register(alloc, &a->visited_in); alloc_register(alloc, &a->visited_out);
    alloc_register(alloc, &a->battle_in); alloc_register(alloc, &a->battle_out);
    alloc_register(alloc, &a->party_in); alloc_register(alloc, &a->party_out);
    alloc_register(alloc, &a->bag_in); alloc_register(alloc, &a->bag_out);
    alloc_register(alloc, &a->species_idx); alloc_register(alloc, &a->item_idx);
    alloc_register(alloc, &a->concat);
    alloc_register(alloc, &a->out);
    if (ew->tiles) {
        a->tl_in = {.shape = {B * PKR_TL_IC * PK_TILE_MAP_CELLS}};
        a->tile_idx = {.shape = {B, PK_TILE_MAP_CELLS}};
        alloc_register(alloc, &a->tl_in);
        alloc_register(alloc, &a->tile_idx);
    }
}

static void* pokered_encoder_create_weights(void* self) {
    Encoder* e = (Encoder*)self;
    PokeredEncoderWeights* ew = (PokeredEncoderWeights*)calloc(
        1, sizeof(PokeredEncoderWeights));
    ew->obs_size = e->in_dim;
    ew->hidden = e->out_dim;
    ew->tiles = g_pokered_obs_tiles > 0;
    ew->d = pkr_dims(ew->tiles);
    ew->concat = ew->d.flat() + PKR_ENC_TAIL;
    return ew;
}

static void create_pokered_conv_encoder(Encoder* enc) {
    *enc = Encoder{
        .forward = pokered_encoder_forward,
        .backward = pokered_encoder_backward,
        .init_weights = pokered_encoder_init_weights,
        .reg_params = pokered_encoder_reg_params,
        .reg_train = pokered_encoder_reg_train,
        .reg_rollout = pokered_encoder_reg_rollout,
        .create_weights = pokered_encoder_create_weights,
        .in_dim = enc->in_dim, .out_dim = enc->out_dim,
        .activation_size = sizeof(PokeredEncoderActivations),
    };
}
