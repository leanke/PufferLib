#define PKR_SPECIES_VOCAB 256
#define PKR_SPECIES_EMBED 8
#define PKR_MOVE_VOCAB 256
#define PKR_MOVE_EMBED 8
#define PKR_ITEM_VOCAB 256
#define PKR_ITEM_EMBED 4
#define PKR_MAP_VOCAB 256
#define PKR_MAP_EMBED 8
#define PKR_TILESET_VOCAB 32
#define PKR_TILESET_EMBED 4
#define PKR_TILE_VOCAB 256
#define PKR_TILE_EMBED 7
#define PKR_LEVEL_SCALE 100.0f
#define PKR_ITEM_COUNT_SCALE 99.0f
#define PKR_FXP_SCALE 4294967296.0

#define PKR_STATUS_BITS 5
#define PKR_MOVE_FEAT (PKR_MOVE_EMBED + 1)
#define PKR_MON_MOVE_OFF (PKR_SPECIES_EMBED + 2 + PKR_STATUS_BITS)
#define PKR_MON_FEAT (PKR_MON_MOVE_OFF + MON_MOVE_SLOTS * PKR_MOVE_FEAT)
#define PKR_MON_SLOTS (PARTY_SIZE + 2)
#define PKR_MOVE_IDX (PKR_MON_SLOTS * MON_MOVE_SLOTS)
#define PKR_BAG_SLOT_FEAT (PKR_ITEM_EMBED + 3)
#define PKR_HM_BITS 5

#define PKR_COORD_SCALE 128.0f
#define PKR_DEX_SCALE 151.0f
#define PKR_MONEY_LOG_MAX 13.815512f
#define PKR_MAP_EVENTS_SCALE 16.0f
#define PKR_INFO_EXTRA (2 + TOWN_BITS + MOVEMENT_COUNT + 1 + 1 + 2 + 1 + 3)
#define PKR_INFO_FEAT \
    (PKR_MAP_EMBED + PKR_TILESET_EMBED + 8 + FACING_COUNT + INFO_PROGRESS_BITS + INFO_KEY_ITEM_BITS + PKR_INFO_EXTRA)
#define PKR_INFO_IN 80
#define PKR_BATTLE_IN 112
#define PKR_PARTY_IN 312
#define PKR_BAG_IN 176

#define PKR_IN_INFO 0
#define PKR_IN_BATTLE (PKR_IN_INFO + PKR_INFO_IN)
#define PKR_IN_PARTY (PKR_IN_BATTLE + PKR_BATTLE_IN)
#define PKR_IN_BAG (PKR_IN_PARTY + PKR_PARTY_IN)
#define PKR_TAIL_IN (PKR_IN_BAG + PKR_BAG_IN)

#define PKR_INFO_HIDDEN 32
#define PKR_BATTLE_HIDDEN 64
#define PKR_PARTY_HIDDEN 128
#define PKR_BAG_HIDDEN 32

#define PKR_OUT_INFO 0
#define PKR_OUT_BATTLE (PKR_OUT_INFO + PKR_INFO_HIDDEN)
#define PKR_OUT_PARTY (PKR_OUT_BATTLE + PKR_BATTLE_HIDDEN)
#define PKR_OUT_BAG (PKR_OUT_PARTY + PKR_PARTY_HIDDEN)
#define PKR_TAIL_OUT (PKR_OUT_BAG + PKR_BAG_HIDDEN)

#define PKR_TL_IC 8
#define PKR_TL_CH_VISITED PKR_TILE_EMBED
#define PKR_TL_GRAD_CH PKR_TILE_EMBED
#define PKR_TL_DX_STRIDE 8
#define PKR_TL_H PK_TILE_MAP_H
#define PKR_TL_W PK_TILE_MAP_W
#define PKR_TL_K1 3
#define PKR_TL_S1 1
#define PKR_TL_OH1 ((PKR_TL_H - PKR_TL_K1) / PKR_TL_S1 + 1)
#define PKR_TL_OW1 ((PKR_TL_W - PKR_TL_K1) / PKR_TL_S1 + 1)

#define PKR_C1_OC 32
#define PKR_C2_OC 64
#define PKR_C2_K 3
#define PKR_C2_S 2

#define PKR_INDEX_PER_ROW (PKR_MON_SLOTS + PKR_MOVE_IDX + BAG_SLOTS + 2 + PK_TILE_MAP_CELLS)

static_assert(PKR_INFO_IN >= PKR_INFO_FEAT, "info input too small");
static_assert(PKR_BATTLE_IN >= BATTLE_TYPE_COUNT + 2 * PKR_MON_FEAT, "battle input too small");
static_assert(PKR_PARTY_IN >= PARTY_SIZE * PKR_MON_FEAT, "party input too small");
static_assert(PKR_BAG_IN >= BAG_SLOTS * PKR_BAG_SLOT_FEAT + PARTY_SIZE * PKR_HM_BITS, "bag input too small");
static_assert(PKR_TL_CH_VISITED + 1 == PKR_TL_IC, "tile channel layout");

struct PkrConvDims {
    int ic, ih, iw;
    int k1, s1, oh1, ow1;
    int oh2, ow2;
    int sp1() const { return oh1 * ow1; }
    int colw1() const { return ic * k1 * k1; }
    int sp2() const { return oh2 * ow2; }
    int colw2() const { return PKR_C1_OC * PKR_C2_K * PKR_C2_K; }
    int flat() const { return PKR_C2_OC * sp2(); }
};

static PkrConvDims pkr_dims() {
    PkrConvDims d;
    d.ic = PKR_TL_IC; d.ih = PKR_TL_H; d.iw = PKR_TL_W; d.k1 = PKR_TL_K1; d.s1 = PKR_TL_S1;
    d.oh1 = (d.ih - d.k1) / d.s1 + 1; d.ow1 = (d.iw - d.k1) / d.s1 + 1;
    d.oh2 = (d.oh1 - PKR_C2_K) / PKR_C2_S + 1; d.ow2 = (d.ow1 - PKR_C2_K) / PKR_C2_S + 1;
    return d;
}

static void pkr_gemm(bool ta, bool tb, int M, int N, int K, const precision_t* A, int lda,
        const precision_t* B, int ldb, precision_t* C, int ldc, float beta, cudaStream_t stream) {
    float alpha = 1.0f;
    puf_cublas_set_stream(g_cublas_handle, stream);
    cublasGemmEx(g_cublas_handle, tb ? CUBLAS_OP_T : CUBLAS_OP_N, ta ? CUBLAS_OP_T : CUBLAS_OP_N,
        N, M, K, &alpha, B, CUBLAS_PRECISION, ldb, A, CUBLAS_PRECISION, lda, &beta,
        C, CUBLAS_PRECISION, ldc, CUBLAS_COMPUTE, CUBLAS_GEMM_DEFAULT);
}

__device__ __forceinline__ int pkr_clamp_id(float v, int vocab) {
    int i = (int)(v + 0.5f);
    i = i < 0 ? 0 : i;
    return i >= vocab ? vocab - 1 : i;
}

__device__ __forceinline__ int pkr_mon_obs_offset(int slot) {
    if (slot < PARTY_SIZE) return PARTY_OBS_OFFSET + slot * MON_FIELDS;
    return BATTLE_OBS_OFFSET + (slot == PARTY_SIZE ? BATTLE_PLAYER_MON_OFFSET : BATTLE_ENEMY_MON_OFFSET);
}

__device__ __forceinline__ int pkr_mon_in_offset(int slot) {
    if (slot < PARTY_SIZE) return PKR_IN_PARTY + slot * PKR_MON_FEAT;
    return PKR_IN_BATTLE + BATTLE_TYPE_COUNT + (slot - PARTY_SIZE) * PKR_MON_FEAT;
}

__global__ void pkr_index_kernel(
        const precision_t* __restrict__ obs, int* __restrict__ species_idx, int* __restrict__ move_idx,
        int* __restrict__ item_idx, int* __restrict__ map_idx, int* __restrict__ tileset_idx,
        int* __restrict__ tile_idx, int B, int obs_size) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= B * PKR_INDEX_PER_ROW) {
        return;
    }
    int b = idx / PKR_INDEX_PER_ROW;
    int k = idx % PKR_INDEX_PER_ROW;
    const precision_t* row = obs + (int64_t)b * obs_size;
    if (k < PKR_MON_SLOTS) {
        species_idx[b * PKR_MON_SLOTS + k] =
            pkr_clamp_id(to_float(row[pkr_mon_obs_offset(k) + MON_SPECIES]), PKR_SPECIES_VOCAB);
        return;
    }
    k -= PKR_MON_SLOTS;
    if (k < PKR_MOVE_IDX) {
        int off = pkr_mon_obs_offset(k / MON_MOVE_SLOTS) + MON_MOVES + k % MON_MOVE_SLOTS;
        move_idx[b * PKR_MOVE_IDX + k] = pkr_clamp_id(to_float(row[off]), PKR_MOVE_VOCAB);
        return;
    }
    k -= PKR_MOVE_IDX;
    if (k < BAG_SLOTS) {
        item_idx[b * BAG_SLOTS + k] = pkr_clamp_id(to_float(row[BAG_OBS_OFFSET + k * BAG_FIELDS + BAG_ITEM]), PKR_ITEM_VOCAB);
        return;
    }
    k -= BAG_SLOTS;
    if (k == 0) {
        map_idx[b] = pkr_clamp_id(to_float(row[INFO_OBS_OFFSET + INFO_MAP]), PKR_MAP_VOCAB);
        return;
    }
    if (k == 1) {
        tileset_idx[b] = pkr_clamp_id(to_float(row[INFO_OBS_OFFSET + INFO_TILESET]), PKR_TILESET_VOCAB);
        return;
    }
    k -= 2;
    tile_idx[b * PK_TILE_MAP_CELLS + k] = pkr_clamp_id(to_float(row[TILE_OBS_OFFSET + k]), PKR_TILE_VOCAB);
}

__device__ __forceinline__ float pkr_mon_feature(
        const precision_t* __restrict__ row, const precision_t* __restrict__ species_w,
        const precision_t* __restrict__ move_w, const int* __restrict__ species_idx,
        const int* __restrict__ move_idx, int b, int slot, int f) {
    if (f < PKR_SPECIES_EMBED) {
        return to_float(species_w[species_idx[b * PKR_MON_SLOTS + slot] * PKR_SPECIES_EMBED + f]);
    }
    int off = pkr_mon_obs_offset(slot);
    if (f >= PKR_MON_MOVE_OFF) {
        f -= PKR_MON_MOVE_OFF;
        int k = f / PKR_MOVE_FEAT, e = f % PKR_MOVE_FEAT;
        if (e == PKR_MOVE_EMBED) return to_float(row[off + MON_MOVE_PP + k]) / PK_OBS_HP_SCALE;
        return to_float(move_w[move_idx[b * PKR_MOVE_IDX + slot * MON_MOVE_SLOTS + k] * PKR_MOVE_EMBED + e]);
    }
    f -= PKR_SPECIES_EMBED;
    if (f == 0) return to_float(row[off + MON_LEVEL]) / PKR_LEVEL_SCALE;
    if (f == 1) return to_float(row[off + MON_HP]) / PK_OBS_HP_SCALE;
    int st = (int)(to_float(row[off + MON_STATUS]) + 0.5f);
    if (f == 2) return (st & PKRED_STATUS_SLP_MASK) ? 1.0f : 0.0f;
    return ((st >> (PKRED_STATUS_PSN_BIT + f - 3)) & 1) ? 1.0f : 0.0f;
}

__device__ __forceinline__ float pkr_info_extra(const precision_t* __restrict__ info, int c) {
    if (c < 2) return to_float(info[INFO_X + c]) / PKR_COORD_SCALE;
    if ((c -= 2) < TOWN_BITS) {
        int towns = (int)(to_float(info[INFO_TOWNS_LO]) + 0.5f) | (int)(to_float(info[INFO_TOWNS_HI]) + 0.5f) << 8;
        return (towns >> c) & 1 ? 1.0f : 0.0f;
    }
    if ((c -= TOWN_BITS) < MOVEMENT_COUNT)
        return pkr_clamp_id(to_float(info[INFO_MOVEMENT]), MOVEMENT_COUNT) == c ? 1.0f : 0.0f;
    if ((c -= MOVEMENT_COUNT) == 0) return to_float(info[INFO_STRENGTH]);
    if (--c == 0) {
        float money = 0.0f;
        for (int i = 0; i < INFO_MONEY_DIGITS; i++)
            money = money * 100.0f + to_float(info[INFO_MONEY + i]);
        return log1pf(money) / PKR_MONEY_LOG_MAX;
    }
    if (--c < 2) return to_float(info[INFO_DEX_OWNED + c]) / PKR_DEX_SCALE;
    if ((c -= 2) == 0) return to_float(info[INFO_EVENTS]) / PK_OBS_HP_SCALE;
    float left = to_float(info[INFO_MAP_EVENTS_LEFT]), total = to_float(info[INFO_MAP_EVENTS_TOTAL]);
    if (c == 1) return fminf(left / PKR_MAP_EVENTS_SCALE, 1.0f);
    if (c == 2) return left > 0.0f ? 1.0f : 0.0f;
    return total > 0.0f ? left / total : 0.0f;
}

struct PkrTailArgs {
    const precision_t* obs;
    const precision_t* species_w;
    const precision_t* move_w;
    const precision_t* item_w;
    const precision_t* map_w;
    const precision_t* tileset_w;
    const int* species_idx;
    const int* move_idx;
    const int* item_idx;
    const int* map_idx;
    const int* tileset_idx;
    precision_t* out;
    int B, obs_size;
};

__global__ void pkr_tail_gather_kernel(PkrTailArgs a) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= a.B * PKR_TAIL_IN) {
        return;
    }
    int b = idx / PKR_TAIL_IN;
    int c = idx % PKR_TAIL_IN;
    const precision_t* row = a.obs + (int64_t)b * a.obs_size;
    float v = 0.0f;
    if (c < PKR_IN_BATTLE) {
        if (c < PKR_MAP_EMBED) {
            v = to_float(a.map_w[a.map_idx[b] * PKR_MAP_EMBED + c]);
        } else if ((c -= PKR_MAP_EMBED) < PKR_TILESET_EMBED) {
            v = to_float(a.tileset_w[a.tileset_idx[b] * PKR_TILESET_EMBED + c]);
        } else if ((c -= PKR_TILESET_EMBED) < 8) {
            v = ((int)(to_float(row[INFO_OBS_OFFSET + INFO_BADGES]) + 0.5f) >> c) & 1 ? 1.0f : 0.0f;
        } else if ((c -= 8) < FACING_COUNT) {
            v = pkr_clamp_id(to_float(row[INFO_OBS_OFFSET + INFO_FACING]), FACING_COUNT) == c ? 1.0f : 0.0f;
        } else if ((c -= FACING_COUNT) < INFO_PROGRESS_BITS) {
            v = to_float(row[INFO_OBS_OFFSET + INFO_PROGRESS + c]);
        } else if ((c -= INFO_PROGRESS_BITS) < INFO_KEY_ITEM_BITS) {
            v = to_float(row[INFO_OBS_OFFSET + INFO_KEY_ITEMS + c]);
        } else if ((c -= INFO_KEY_ITEM_BITS) < PKR_INFO_EXTRA) {
            v = pkr_info_extra(row + INFO_OBS_OFFSET, c);
        }
    } else if (c < PKR_IN_PARTY) {
        c -= PKR_IN_BATTLE;
        if (c < BATTLE_TYPE_COUNT) {
            v = pkr_clamp_id(to_float(row[BATTLE_OBS_OFFSET]), BATTLE_TYPE_COUNT) == c ? 1.0f : 0.0f;
        } else if ((c -= BATTLE_TYPE_COUNT) < 2 * PKR_MON_FEAT) {
            v = pkr_mon_feature(row, a.species_w, a.move_w, a.species_idx, a.move_idx, b,
                PARTY_SIZE + c / PKR_MON_FEAT, c % PKR_MON_FEAT);
        }
    } else if (c < PKR_IN_BAG) {
        c -= PKR_IN_PARTY;
        if (c < PARTY_SIZE * PKR_MON_FEAT) {
            v = pkr_mon_feature(row, a.species_w, a.move_w, a.species_idx, a.move_idx, b,
                c / PKR_MON_FEAT, c % PKR_MON_FEAT);
        }
    } else {
        c -= PKR_IN_BAG;
        int s = c / PKR_BAG_SLOT_FEAT, f = c % PKR_BAG_SLOT_FEAT;
        if (s < BAG_SLOTS) {
            const precision_t* slot = row + BAG_OBS_OFFSET + s * BAG_FIELDS;
            if (f < PKR_ITEM_EMBED) {
                v = to_float(a.item_w[a.item_idx[b * BAG_SLOTS + s] * PKR_ITEM_EMBED + f]);
            } else if ((f -= PKR_ITEM_EMBED) == 0) {
                v = to_float(slot[BAG_COUNT]) / PKR_ITEM_COUNT_SCALE;
            } else {
                v = to_float(slot[f == 1 ? BAG_USABLE : BAG_CURSOR]);
            }
        } else if ((c -= BAG_SLOTS * PKR_BAG_SLOT_FEAT) < PARTY_SIZE * PKR_HM_BITS) {
            int mask = (int)(to_float(row[BAG_OBS_OFFSET + BAG_HM_LEARNABLE + c / PKR_HM_BITS]) + 0.5f);
            v = (mask >> (c % PKR_HM_BITS)) & 1 ? 1.0f : 0.0f;
        }
    }
    a.out[idx] = from_float(v);
}

constexpr int PKR_VEC = 16 / sizeof(precision_t);

__global__ void pkr_tiles_col1_kernel(
        const precision_t* __restrict__ obs, const precision_t* __restrict__ tile_w,
        const int* __restrict__ tile_idx, precision_t* __restrict__ col, int B, int obs_size) {
    constexpr int TAPS = PKR_TL_K1 * PKR_TL_K1;
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= B * PKR_TL_OH1 * PKR_TL_OW1 * TAPS) {
        return;
    }
    int tap = idx % TAPS;
    int row = idx / TAPS;
    int ow = row % PKR_TL_OW1;
    int oh = (row / PKR_TL_OW1) % PKR_TL_OH1;
    int b = row / (PKR_TL_OH1 * PKR_TL_OW1);
    int ih = oh * PKR_TL_S1 + tap / PKR_TL_K1;
    int iw = ow * PKR_TL_S1 + tap % PKR_TL_K1;
    int cell = ih * PKR_TL_W + iw;
    const precision_t* r = obs + (int64_t)b * obs_size;
    __align__(16) precision_t v[PKR_TL_IC];
    const precision_t* te = tile_w + tile_idx[b * PK_TILE_MAP_CELLS + cell] * PKR_TILE_EMBED;
#pragma unroll
    for (int c = 0; c < PKR_TILE_EMBED; c++) {
        v[c] = te[c];
    }
    v[PKR_TL_CH_VISITED] = r[VISITED_MASK_OFFSET + (ih / 2) * BLOCKS_WIDE + iw / 2];
    uint4* dst = (uint4*)(col + (int64_t)idx * PKR_TL_IC);
    const uint4* src = (const uint4*)v;
#pragma unroll
    for (int i = 0; i < PKR_TL_IC / PKR_VEC; i++) {
        dst[i] = src[i];
    }
}

template <int IC, int IH, int IW, int K, int S, int OH, int OW>
__global__ void pkr_im2col_relu_nhwc(const precision_t* __restrict__ input, precision_t* __restrict__ col, int B) {
    constexpr int GROUPS = IC / PKR_VEC;
    static_assert(IC % PKR_VEC == 0, "channel count must fill 16-byte vectors");
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= B * OH * OW * K * K * GROUPS) {
        return;
    }
    int g = idx % GROUPS;
    int tap = (idx / GROUPS) % (K * K);
    int row = idx / (GROUPS * K * K);
    int ow = row % OW;
    int oh = (row / OW) % OH;
    int b = row / (OH * OW);
    int ih = oh * S + tap / K;
    int iw = ow * S + tap % K;
    uint4 raw = ((const uint4*)(input + ((int64_t)(b * IH + ih) * IW + iw) * IC))[g];
    precision_t* v = (precision_t*)&raw;
#pragma unroll
    for (int i = 0; i < PKR_VEC; i++) {
        v[i] = to_float(v[i]) > 0.0f ? v[i] : from_float(0.0f);
    }
    ((uint4*)col)[idx] = raw;
}

template <int IC, int IH, int IW, int K, int S, int OH, int OW>
__global__ void pkr_col2im_nhwc(const precision_t* __restrict__ col, precision_t* __restrict__ grad_input, int B) {
    constexpr int COLW = IC * K * K;
    constexpr int GROUPS = IC / PKR_VEC;
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= B * IH * IW * GROUPS) {
        return;
    }
    int g = idx % GROUPS;
    int iw = (idx / GROUPS) % IW;
    int ih = (idx / (GROUPS * IW)) % IH;
    int b = idx / (GROUPS * IW * IH);
    float sum[PKR_VEC];
#pragma unroll
    for (int i = 0; i < PKR_VEC; i++) {
        sum[i] = 0.0f;
    }
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
            int64_t row = (int64_t)(b * OH + oh_num / S) * OW + ow_num / S;
            uint4 raw = ((const uint4*)(col + row * COLW + (kh * K + kw) * IC))[g];
            const precision_t* v = (const precision_t*)&raw;
#pragma unroll
            for (int i = 0; i < PKR_VEC; i++) {
                sum[i] += to_float(v[i]);
            }
        }
    }
    uint4 out;
    precision_t* o = (precision_t*)&out;
#pragma unroll
    for (int i = 0; i < PKR_VEC; i++) {
        o[i] = from_float(sum[i]);
    }
    ((uint4*)grad_input)[idx] = out;
}

__global__ void pkr_tl_input_grad_kernel(
        const precision_t* __restrict__ conv1_grad, const precision_t* __restrict__ conv1_w,
        float* __restrict__ dx, int B) {
    constexpr int TAPS = PKR_TL_K1 * PKR_TL_K1;
    constexpr int COLW = PKR_TL_IC * TAPS;
    constexpr int Q = PKR_TL_DX_STRIDE / 4;
    __shared__ float4 w[TAPS * PKR_C1_OC * Q];
    float* wf = (float*)w;
    for (int i = threadIdx.x; i < TAPS * PKR_C1_OC * PKR_TL_DX_STRIDE; i += blockDim.x) {
        int ch = i % PKR_TL_DX_STRIDE, oc = (i / PKR_TL_DX_STRIDE) % PKR_C1_OC;
        int tap = i / (PKR_TL_DX_STRIDE * PKR_C1_OC);
        wf[i] = ch < PKR_TL_GRAD_CH ? to_float(conv1_w[oc * COLW + tap * PKR_TL_IC + ch]) : 0.0f;
    }
    __syncthreads();
    int pos = blockIdx.x * blockDim.x + threadIdx.x;
    if (pos >= B * PK_TILE_MAP_CELLS) {
        return;
    }
    int b = pos / PK_TILE_MAP_CELLS;
    int cell = pos % PK_TILE_MAP_CELLS;
    int ih = cell / PKR_TL_W;
    int iw = cell % PKR_TL_W;
    float4 acc[Q];
#pragma unroll
    for (int q = 0; q < Q; q++) {
        acc[q] = make_float4(0.0f, 0.0f, 0.0f, 0.0f);
    }
#pragma unroll
    for (int kh = 0; kh < PKR_TL_K1; kh++) {
        int oh = ih - kh;
        if (oh < 0 || oh >= PKR_TL_OH1) {
            continue;
        }
#pragma unroll
        for (int kw = 0; kw < PKR_TL_K1; kw++) {
            int ow = iw - kw;
            if (ow < 0 || ow >= PKR_TL_OW1) {
                continue;
            }
            const float4* wt = w + (kh * PKR_TL_K1 + kw) * PKR_C1_OC * Q;
            const uint4* dyv = (const uint4*)(conv1_grad + ((int64_t)(b * PKR_TL_OH1 + oh) * PKR_TL_OW1 + ow) * PKR_C1_OC);
#pragma unroll
            for (int gi = 0; gi < PKR_C1_OC / PKR_VEC; gi++) {
                uint4 raw = dyv[gi];
                const precision_t* dy = (const precision_t*)&raw;
#pragma unroll
                for (int j = 0; j < PKR_VEC; j++) {
                    float d = to_float(dy[j]);
                    const float4* wo = wt + (gi * PKR_VEC + j) * Q;
#pragma unroll
                    for (int q = 0; q < Q; q++) {
                        float4 c = wo[q];
                        acc[q].x += d * c.x; acc[q].y += d * c.y; acc[q].z += d * c.z; acc[q].w += d * c.w;
                    }
                }
            }
        }
    }
    float4* out = (float4*)(dx + (int64_t)pos * PKR_TL_DX_STRIDE);
#pragma unroll
    for (int q = 0; q < Q; q++) {
        out[q] = acc[q];
    }
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
        bool uniform = peers == 0xffffffffu;
#pragma unroll
        for (int e = 0; e < E; e++) {
            long long mine = v >= 0 ? (long long)pkr_fxp(g[e]) : 0;
            long long sum = 0;
            if (uniform) {
                sum = mine;
#pragma unroll
                for (int o = 16; o; o >>= 1) {
                    sum += __shfl_xor_sync(0xffffffffu, sum, o);
                }
            } else {
                for (unsigned m = peers; m; m &= m - 1) {
                    sum += __shfl_sync(peers, mine, __ffs(m) - 1);
                }
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
    const precision_t* tail_in;
    __device__ void operator()(int pos, float* g) const {
        const precision_t* src = tail_in + (int64_t)(pos / PKR_MON_SLOTS) * PKR_TAIL_IN
            + pkr_mon_in_offset(pos % PKR_MON_SLOTS);
#pragma unroll
        for (int e = 0; e < PKR_SPECIES_EMBED; e++) {
            g[e] = to_float(src[e]);
        }
    }
};

struct PkrMoveGrad {
    const precision_t* tail_in;
    __device__ void operator()(int pos, float* g) const {
        int r = pos % PKR_MOVE_IDX;
        const precision_t* src = tail_in + (int64_t)(pos / PKR_MOVE_IDX) * PKR_TAIL_IN
            + pkr_mon_in_offset(r / MON_MOVE_SLOTS) + PKR_MON_MOVE_OFF + (r % MON_MOVE_SLOTS) * PKR_MOVE_FEAT;
#pragma unroll
        for (int e = 0; e < PKR_MOVE_EMBED; e++) {
            g[e] = to_float(src[e]);
        }
    }
};

template <int E>
struct PkrSlotGrad {
    const precision_t* tail_in;
    int width, offset, slots, slot_stride;
    __device__ void operator()(int pos, float* g) const {
        const precision_t* src = tail_in + (int64_t)(pos / slots) * width + offset + (pos % slots) * slot_stride;
#pragma unroll
        for (int e = 0; e < E; e++) {
            g[e] = to_float(src[e]);
        }
    }
};

template <int E>
struct PkrDxGrad {
    const float* dx;
    int offset;
    __device__ void operator()(int pos, float* g) const {
#pragma unroll
        for (int e = 0; e < E; e++) {
            g[e] = dx[(int64_t)pos * PKR_TL_DX_STRIDE + offset + e];
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

__global__ void pkr_relu_kernel(precision_t* __restrict__ data, int64_t total) {
    int64_t idx = (int64_t)blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= total) {
        return;
    }
    data[idx] = from_float(fmaxf(0.0f, to_float(data[idx])));
}

__global__ void pkr_relu_backward_kernel(
        precision_t* __restrict__ grad, const precision_t* __restrict__ out, int64_t total) {
    int64_t idx = (int64_t)blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= total) {
        return;
    }
    if (to_float(out[idx]) <= 0.0f) {
        grad[idx] = from_float(0.0f);
    }
}

static void pkr_relu(precision_t* data, int64_t n, cudaStream_t stream) {
    pkr_relu_kernel<<<grid_size(n), BLOCK_SIZE, 0, stream>>>(data, n);
}

static void pkr_relu_backward(precision_t* grad, const precision_t* out, int64_t n, cudaStream_t stream) {
    pkr_relu_backward_kernel<<<grid_size(n), BLOCK_SIZE, 0, stream>>>(grad, out, n);
}

struct PkrBranch {
    int in_off, in_dim, out_off, hidden;
};

static const PkrBranch PKR_BRANCHES[] = {
    {PKR_IN_INFO, PKR_INFO_IN, PKR_OUT_INFO, PKR_INFO_HIDDEN},
    {PKR_IN_BATTLE, PKR_BATTLE_IN, PKR_OUT_BATTLE, PKR_BATTLE_HIDDEN},
    {PKR_IN_PARTY, PKR_PARTY_IN, PKR_OUT_PARTY, PKR_PARTY_HIDDEN},
    {PKR_IN_BAG, PKR_BAG_IN, PKR_OUT_BAG, PKR_BAG_HIDDEN},
};
#define PKR_N_BRANCHES 4

struct PokeredEncoderWeights {
    Prec conv1_w, conv2_w;
    Prec branch_w[PKR_N_BRANCHES];
    Prec species_embed_w, move_embed_w, item_embed_w, map_embed_w, tileset_embed_w, tile_embed_w;
    Prec proj_conv_w, proj_tail_w;
    int obs_size, hidden;
    PkrConvDims d;
};

struct PokeredEncoderActivations {
    Prec col1, mm1, conv1_grad, conv1_wgrad;
    Prec col2, mm2, conv2_grad, conv2_wgrad;
    Prec tail_in, tail, tail_grad;
    Prec branch_wgrad[PKR_N_BRANCHES];
    Int species_idx, move_idx, item_idx, map_idx, tileset_idx, tile_idx;
    Prec species_embed_wgrad, move_embed_wgrad, item_embed_wgrad, map_embed_wgrad, tileset_embed_wgrad;
    Prec tile_embed_wgrad;
    Long species_wgrad_i, move_wgrad_i, item_wgrad_i, map_wgrad_i, tileset_wgrad_i, tile_wgrad_i;
    Float tile_dx;
    Prec out, proj_conv_wgrad, proj_tail_wgrad;
};

#define PKR_TL_C2_ARGS PKR_C1_OC, PKR_TL_OH1, PKR_TL_OW1, PKR_C2_K, PKR_C2_S, \
    (PKR_TL_OH1 - PKR_C2_K) / PKR_C2_S + 1, (PKR_TL_OW1 - PKR_C2_K) / PKR_C2_S + 1

static void pkr_conv_forward(PokeredEncoderWeights* ew, PokeredEncoderActivations* a,
        const precision_t* obs, int B, cudaStream_t stream) {
    const PkrConvDims& d = ew->d;
    int r1 = B * d.sp1(), r2 = B * d.sp2();
    pkr_tiles_col1_kernel<<<grid_size((int64_t)r1 * PKR_TL_K1 * PKR_TL_K1), BLOCK_SIZE, 0, stream>>>(
        obs, ew->tile_embed_w.data, a->tile_idx.data, a->col1.data, B, ew->obs_size);
    pkr_gemm(false, true, r1, PKR_C1_OC, d.colw1(), a->col1.data, d.colw1(), ew->conv1_w.data, d.colw1(),
        a->mm1.data, PKR_C1_OC, 0.0f, stream);
    int n2 = grid_size((int64_t)r2 * d.colw2() / PKR_VEC);
    pkr_im2col_relu_nhwc<PKR_TL_C2_ARGS><<<n2, BLOCK_SIZE, 0, stream>>>(a->mm1.data, a->col2.data, B);
    pkr_gemm(false, true, r2, PKR_C2_OC, d.colw2(), a->col2.data, d.colw2(), ew->conv2_w.data, d.colw2(),
        a->mm2.data, PKR_C2_OC, 0.0f, stream);
    pkr_relu(a->mm2.data, (int64_t)r2 * PKR_C2_OC, stream);
}

static void pkr_conv_backward(PokeredEncoderWeights* ew, PokeredEncoderActivations* a, int B, cudaStream_t stream) {
    const PkrConvDims& d = ew->d;
    int r1 = B * d.sp1(), r2 = B * d.sp2();
    pkr_gemm(true, false, PKR_C2_OC, d.colw2(), r2, a->conv2_grad.data, PKR_C2_OC, a->col2.data, d.colw2(),
        a->conv2_wgrad.data, d.colw2(), 0.0f, stream);
    pkr_gemm(false, false, r2, d.colw2(), PKR_C2_OC, a->conv2_grad.data, PKR_C2_OC, ew->conv2_w.data, d.colw2(),
        a->col2.data, d.colw2(), 0.0f, stream);
    int n1 = grid_size((int64_t)r1 * PKR_C1_OC / PKR_VEC);
    pkr_col2im_nhwc<PKR_TL_C2_ARGS><<<n1, BLOCK_SIZE, 0, stream>>>(a->col2.data, a->conv1_grad.data, B);
    pkr_relu_backward(a->conv1_grad.data, a->mm1.data, (int64_t)r1 * PKR_C1_OC, stream);
    pkr_gemm(true, false, PKR_C1_OC, d.colw1(), r1, a->conv1_grad.data, PKR_C1_OC, a->col1.data, d.colw1(),
        a->conv1_wgrad.data, d.colw1(), 0.0f, stream);
    pkr_tl_input_grad_kernel<<<grid_size(B * PK_TILE_MAP_CELLS), BLOCK_SIZE, 0, stream>>>(
        a->conv1_grad.data, ew->conv1_w.data, a->tile_dx.data, B);
    pkr_embed_wgrad<PKR_TILE_VOCAB, PKR_TILE_EMBED>(&a->tile_wgrad_i, &a->tile_embed_wgrad,
        a->tile_idx.data, B * PK_TILE_MAP_CELLS, PkrDxGrad<PKR_TILE_EMBED>{a->tile_dx.data, 0}, stream);
}

static Prec pokered_encoder_forward(void* w, void* activations, Prec input, cudaStream_t stream) {
    PokeredEncoderWeights* ew = (PokeredEncoderWeights*)w;
    PokeredEncoderActivations* a = (PokeredEncoderActivations*)activations;
    int B = input.shape[0];

    pkr_index_kernel<<<grid_size(B * PKR_INDEX_PER_ROW), BLOCK_SIZE, 0, stream>>>(
        input.data, a->species_idx.data, a->move_idx.data, a->item_idx.data, a->map_idx.data,
        a->tileset_idx.data, a->tile_idx.data, B, ew->obs_size);

    pkr_conv_forward(ew, a, input.data, B, stream);

    PkrTailArgs ta = {input.data, ew->species_embed_w.data, ew->move_embed_w.data, ew->item_embed_w.data,
        ew->map_embed_w.data, ew->tileset_embed_w.data, a->species_idx.data, a->move_idx.data, a->item_idx.data,
        a->map_idx.data, a->tileset_idx.data, a->tail_in.data, B, ew->obs_size};
    pkr_tail_gather_kernel<<<grid_size(B * PKR_TAIL_IN), BLOCK_SIZE, 0, stream>>>(ta);
    for (int i = 0; i < PKR_N_BRANCHES; i++) {
        const PkrBranch& br = PKR_BRANCHES[i];
        pkr_gemm(false, true, B, br.hidden, br.in_dim, a->tail_in.data + br.in_off, PKR_TAIL_IN,
            ew->branch_w[i].data, br.in_dim, a->tail.data + br.out_off, PKR_TAIL_OUT, 0.0f, stream);
    }
    pkr_relu(a->tail.data, (int64_t)B * PKR_TAIL_OUT, stream);

    int flat = ew->d.flat();
    pkr_gemm(false, true, B, ew->hidden, flat, a->mm2.data, flat, ew->proj_conv_w.data, flat,
        a->out.data, ew->hidden, 0.0f, stream);
    pkr_gemm(false, true, B, ew->hidden, PKR_TAIL_OUT, a->tail.data, PKR_TAIL_OUT, ew->proj_tail_w.data,
        PKR_TAIL_OUT, a->out.data, ew->hidden, 1.0f, stream);
    pkr_relu(a->out.data, (int64_t)B * ew->hidden, stream);
    return a->out;
}

static void pokered_encoder_backward(void* w, void* activations, Prec grad, cudaStream_t stream) {
    PokeredEncoderWeights* ew = (PokeredEncoderWeights*)w;
    PokeredEncoderActivations* a = (PokeredEncoderActivations*)activations;
    int B = grad.shape[0];
    int H = ew->hidden;
    int flat = ew->d.flat();
    int T = PKR_TAIL_OUT;
    int W = PKR_TAIL_IN;

    pkr_relu_backward(grad.data, a->out.data, (int64_t)B * H, stream);
    pkr_gemm(true, false, H, flat, B, grad.data, H, a->mm2.data, flat, a->proj_conv_wgrad.data, flat, 0.0f, stream);
    pkr_gemm(true, false, H, T, B, grad.data, H, a->tail.data, T, a->proj_tail_wgrad.data, T, 0.0f, stream);
    pkr_gemm(false, false, B, flat, H, grad.data, H, ew->proj_conv_w.data, flat, a->conv2_grad.data, flat,
        0.0f, stream);
    pkr_gemm(false, false, B, T, H, grad.data, H, ew->proj_tail_w.data, T, a->tail_grad.data, T, 0.0f, stream);
    pkr_relu_backward(a->conv2_grad.data, a->mm2.data, (int64_t)B * flat, stream);
    pkr_relu_backward(a->tail_grad.data, a->tail.data, (int64_t)B * T, stream);

    for (int i = 0; i < PKR_N_BRANCHES; i++) {
        const PkrBranch& br = PKR_BRANCHES[i];
        pkr_gemm(true, false, br.hidden, br.in_dim, B, a->tail_grad.data + br.out_off, T,
            a->tail_in.data + br.in_off, W, a->branch_wgrad[i].data, br.in_dim, 0.0f, stream);
        pkr_gemm(false, false, B, br.in_dim, br.hidden, a->tail_grad.data + br.out_off, T,
            ew->branch_w[i].data, br.in_dim, a->tail_in.data + br.in_off, W, 0.0f, stream);
    }

    pkr_embed_wgrad<PKR_SPECIES_VOCAB, PKR_SPECIES_EMBED>(&a->species_wgrad_i, &a->species_embed_wgrad,
        a->species_idx.data, B * PKR_MON_SLOTS, PkrSpeciesGrad{a->tail_in.data}, stream);
    pkr_embed_wgrad<PKR_MOVE_VOCAB, PKR_MOVE_EMBED>(&a->move_wgrad_i, &a->move_embed_wgrad,
        a->move_idx.data, B * PKR_MOVE_IDX, PkrMoveGrad{a->tail_in.data}, stream);
    pkr_embed_wgrad<PKR_ITEM_VOCAB, PKR_ITEM_EMBED>(&a->item_wgrad_i, &a->item_embed_wgrad,
        a->item_idx.data, B * BAG_SLOTS,
        PkrSlotGrad<PKR_ITEM_EMBED>{a->tail_in.data, W, PKR_IN_BAG, BAG_SLOTS, PKR_BAG_SLOT_FEAT}, stream);
    pkr_embed_wgrad<PKR_MAP_VOCAB, PKR_MAP_EMBED>(&a->map_wgrad_i, &a->map_embed_wgrad,
        a->map_idx.data, B, PkrSlotGrad<PKR_MAP_EMBED>{a->tail_in.data, W, PKR_IN_INFO, 1, 0}, stream);
    pkr_embed_wgrad<PKR_TILESET_VOCAB, PKR_TILESET_EMBED>(&a->tileset_wgrad_i, &a->tileset_embed_wgrad,
        a->tileset_idx.data, B,
        PkrSlotGrad<PKR_TILESET_EMBED>{a->tail_in.data, W, PKR_IN_INFO + PKR_MAP_EMBED, 1, 0}, stream);

    pkr_conv_backward(ew, a, B, stream);
}

static void pkr_weight_shapes(PokeredEncoderWeights* ew) {
    ew->conv1_w = {.shape = {PKR_C1_OC, ew->d.colw1()}};
    ew->conv2_w = {.shape = {PKR_C2_OC, ew->d.colw2()}};
    for (int i = 0; i < PKR_N_BRANCHES; i++) {
        ew->branch_w[i] = {.shape = {PKR_BRANCHES[i].hidden, PKR_BRANCHES[i].in_dim}};
    }
    ew->species_embed_w = {.shape = {PKR_SPECIES_VOCAB, PKR_SPECIES_EMBED}};
    ew->move_embed_w = {.shape = {PKR_MOVE_VOCAB, PKR_MOVE_EMBED}};
    ew->item_embed_w = {.shape = {PKR_ITEM_VOCAB, PKR_ITEM_EMBED}};
    ew->map_embed_w = {.shape = {PKR_MAP_VOCAB, PKR_MAP_EMBED}};
    ew->tileset_embed_w = {.shape = {PKR_TILESET_VOCAB, PKR_TILESET_EMBED}};
    ew->tile_embed_w = {.shape = {PKR_TILE_VOCAB, PKR_TILE_EMBED}};
    ew->proj_conv_w = {.shape = {ew->hidden, ew->d.flat()}};
    ew->proj_tail_w = {.shape = {ew->hidden, PKR_TAIL_OUT}};
}

static void pokered_encoder_init_weights(void* w, uint64_t* seed, cudaStream_t stream) {
    PokeredEncoderWeights* ew = (PokeredEncoderWeights*)w;
    puf_kaiming_init(&ew->conv1_w, sqrtf(2.0f), (*seed)++, stream);
    puf_kaiming_init(&ew->conv2_w, sqrtf(2.0f), (*seed)++, stream);
    for (int i = 0; i < PKR_N_BRANCHES; i++) {
        puf_kaiming_init(&ew->branch_w[i], sqrtf(2.0f), (*seed)++, stream);
    }
    puf_normal_init(&ew->species_embed_w, 0.02f, (*seed)++, stream);
    puf_normal_init(&ew->move_embed_w, 0.02f, (*seed)++, stream);
    puf_normal_init(&ew->item_embed_w, 0.02f, (*seed)++, stream);
    puf_normal_init(&ew->map_embed_w, 0.02f, (*seed)++, stream);
    puf_normal_init(&ew->tileset_embed_w, 0.02f, (*seed)++, stream);
    puf_normal_init(&ew->tile_embed_w, 0.02f, (*seed)++, stream);
    puf_kaiming_init(&ew->proj_conv_w, sqrtf(2.0f), (*seed)++, stream);
    puf_kaiming_init(&ew->proj_tail_w, sqrtf(2.0f), (*seed)++, stream);
}

static void pokered_encoder_reg_params(void* w, Allocator* alloc) {
    PokeredEncoderWeights* ew = (PokeredEncoderWeights*)w;
    pkr_weight_shapes(ew);
    alloc_register(alloc, &ew->conv1_w);
    alloc_register(alloc, &ew->conv2_w);
    for (int i = 0; i < PKR_N_BRANCHES; i++) {
        alloc_register(alloc, &ew->branch_w[i]);
    }
    alloc_register(alloc, &ew->species_embed_w);
    alloc_register(alloc, &ew->move_embed_w);
    alloc_register(alloc, &ew->item_embed_w);
    alloc_register(alloc, &ew->map_embed_w);
    alloc_register(alloc, &ew->tileset_embed_w);
    alloc_register(alloc, &ew->tile_embed_w);
    alloc_register(alloc, &ew->proj_conv_w);
    alloc_register(alloc, &ew->proj_tail_w);
}

static void pkr_reg_forward_acts(PokeredEncoderWeights* ew, PokeredEncoderActivations* a, Allocator* acts, int B) {
    const PkrConvDims& d = ew->d;
    a->col1 = {.shape = {B * d.sp1(), d.colw1()}};
    a->mm1 = {.shape = {B * d.sp1(), PKR_C1_OC}};
    a->col2 = {.shape = {B * d.sp2(), d.colw2()}};
    a->mm2 = {.shape = {B * d.sp2(), PKR_C2_OC}};
    a->tail_in = {.shape = {B, PKR_TAIL_IN}};
    a->tail = {.shape = {B, PKR_TAIL_OUT}};
    a->out = {.shape = {B, ew->hidden}};
    a->species_idx = {.shape = {B, PKR_MON_SLOTS}};
    a->move_idx = {.shape = {B, PKR_MOVE_IDX}};
    a->item_idx = {.shape = {B, BAG_SLOTS}};
    a->map_idx = {.shape = {B}};
    a->tileset_idx = {.shape = {B}};
    a->tile_idx = {.shape = {B, PK_TILE_MAP_CELLS}};
    alloc_register(acts, &a->col1); alloc_register(acts, &a->mm1);
    alloc_register(acts, &a->col2); alloc_register(acts, &a->mm2);
    alloc_register(acts, &a->tail_in); alloc_register(acts, &a->tail); alloc_register(acts, &a->out);
    alloc_register(acts, &a->species_idx); alloc_register(acts, &a->move_idx); alloc_register(acts, &a->item_idx);
    alloc_register(acts, &a->map_idx); alloc_register(acts, &a->tileset_idx); alloc_register(acts, &a->tile_idx);
}

static void pkr_reg_wgrad(Allocator* acts, Allocator* grads, Prec* wgrad, Long* scratch, int V, int E) {
    *wgrad = {.shape = {V, E}};
    *scratch = {.shape = {V * E}};
    alloc_register(grads, wgrad);
    alloc_register(acts, scratch);
}

static void pokered_encoder_reg_train(void* w, void* activations, Allocator* acts, Allocator* grads, int B_TT) {
    PokeredEncoderWeights* ew = (PokeredEncoderWeights*)w;
    PokeredEncoderActivations* a = (PokeredEncoderActivations*)activations;
    const PkrConvDims& d = ew->d;
    pkr_reg_forward_acts(ew, a, acts, B_TT);

    a->conv1_grad = {.shape = {B_TT * d.sp1(), PKR_C1_OC}};
    a->conv2_grad = {.shape = {B_TT * d.sp2(), PKR_C2_OC}};
    a->tail_grad = {.shape = {B_TT, PKR_TAIL_OUT}};
    alloc_register(acts, &a->conv1_grad); alloc_register(acts, &a->conv2_grad); alloc_register(acts, &a->tail_grad);

    a->conv1_wgrad = {.shape = {PKR_C1_OC, d.colw1()}};
    a->conv2_wgrad = {.shape = {PKR_C2_OC, d.colw2()}};
    alloc_register(grads, &a->conv1_wgrad);
    alloc_register(grads, &a->conv2_wgrad);
    for (int i = 0; i < PKR_N_BRANCHES; i++) {
        a->branch_wgrad[i] = {.shape = {PKR_BRANCHES[i].hidden, PKR_BRANCHES[i].in_dim}};
        alloc_register(grads, &a->branch_wgrad[i]);
    }
    pkr_reg_wgrad(acts, grads, &a->species_embed_wgrad, &a->species_wgrad_i, PKR_SPECIES_VOCAB, PKR_SPECIES_EMBED);
    pkr_reg_wgrad(acts, grads, &a->move_embed_wgrad, &a->move_wgrad_i, PKR_MOVE_VOCAB, PKR_MOVE_EMBED);
    pkr_reg_wgrad(acts, grads, &a->item_embed_wgrad, &a->item_wgrad_i, PKR_ITEM_VOCAB, PKR_ITEM_EMBED);
    pkr_reg_wgrad(acts, grads, &a->map_embed_wgrad, &a->map_wgrad_i, PKR_MAP_VOCAB, PKR_MAP_EMBED);
    pkr_reg_wgrad(acts, grads, &a->tileset_embed_wgrad, &a->tileset_wgrad_i, PKR_TILESET_VOCAB,
        PKR_TILESET_EMBED);
    pkr_reg_wgrad(acts, grads, &a->tile_embed_wgrad, &a->tile_wgrad_i, PKR_TILE_VOCAB, PKR_TILE_EMBED);
    a->proj_conv_wgrad = {.shape = {ew->hidden, d.flat()}};
    a->proj_tail_wgrad = {.shape = {ew->hidden, PKR_TAIL_OUT}};
    alloc_register(grads, &a->proj_conv_wgrad);
    alloc_register(grads, &a->proj_tail_wgrad);
    a->tile_dx = {.shape = {B_TT * PK_TILE_MAP_CELLS * PKR_TL_DX_STRIDE}};
    alloc_register(acts, &a->tile_dx);
}

static void pokered_encoder_reg_rollout(void* w, void* activations, Allocator* alloc, int B) {
    pkr_reg_forward_acts((PokeredEncoderWeights*)w, (PokeredEncoderActivations*)activations, alloc, B);
}

static void* pokered_encoder_create_weights(void* self) {
    Encoder* e = (Encoder*)self;
    PokeredEncoderWeights* ew = (PokeredEncoderWeights*)calloc(1, sizeof(PokeredEncoderWeights));
    ew->obs_size = e->in_dim;
    ew->hidden = e->out_dim;
    ew->d = pkr_dims();
    if (ew->obs_size != TOTAL_OBSERVATIONS) {
        fprintf(stderr, "pokered encoder: obs size %d does not match layout %d\n", ew->obs_size, TOTAL_OBSERVATIONS);
        exit(1);
    }
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
