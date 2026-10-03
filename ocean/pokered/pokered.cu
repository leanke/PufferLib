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

// Species ids are embedded with one table shared by the party and both battle mons;
// item ids are embedded with a second table. Level is scaled by /100, quantity by /99.
#define PKR_SPECIES_VOCAB 256
#define PKR_SPECIES_EMBED_DIM 8
#define PKR_ITEM_VOCAB 256
#define PKR_ITEM_EMBED_DIM 4
#define PKR_LEVEL_SCALE 100.0f
#define PKR_ITEM_COUNT_SCALE 99.0f

// Species slots: 0..PARTY_SIZE-1 are the party, then the player's and opponent's battle mon.
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

__global__ void pkr_rows_to_nchw(
        const precision_t* __restrict__ src, precision_t* __restrict__ dst,
        int B, int OC, int spatial, int relu) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= B * OC * spatial) {
        return;
    }
    int b = idx / (OC * spatial);
    int oc = (idx / spatial) % OC;
    int s = idx % spatial;
    float value = to_float(src[(b * spatial + s) * OC + oc]);
    if (relu) {
        value = fmaxf(0.0f, value);
    }
    dst[idx] = from_float(value);
}

__global__ void pkr_nchw_to_rows(
        const precision_t* __restrict__ src, precision_t* __restrict__ dst,
        int B, int OC, int spatial) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= B * OC * spatial) {
        return;
    }
    int b = idx / (OC * spatial);
    int oc = (idx / spatial) % OC;
    int s = idx % spatial;
    dst[(b * spatial + s) * OC + oc] = src[idx];
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

// Integer ids pulled out of the observation: species per mon slot, item per bag slot.
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

// One mon slot: species embedding, level / 100, hp fraction.
__device__ __forceinline__ float pkr_mon_feature(
        const precision_t* __restrict__ row, const precision_t* __restrict__ species_embed_w,
        const int* __restrict__ species_idx, int b, int slot, int f) {
    if (f < PKR_SPECIES_EMBED_DIM) {
        return to_float(species_embed_w[species_idx[b * PKR_MON_SLOTS + slot]
            * PKR_SPECIES_EMBED_DIM + f]);
    }
    int off = pkr_mon_obs_offset(slot);
    if (f == PKR_SPECIES_EMBED_DIM) return to_float(row[off + 1]) / PKR_LEVEL_SCALE;
    return to_float(row[off + 2]);
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

// battle_type one-hot, then the player's and the opponent's mon.
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
        const precision_t* __restrict__ bag_hidden, int B) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= B * PKR_ENC_CONCAT) {
        return;
    }
    int b = idx / PKR_ENC_CONCAT;
    int c = idx % PKR_ENC_CONCAT;
    if (c < PKR_ENC_CONV_FLAT) { out[idx] = conv_flat[b * PKR_ENC_CONV_FLAT + c]; return; }
    c -= PKR_ENC_CONV_FLAT;
    if (c < PKR_ENC_VISITED_HIDDEN) { out[idx] = visited_hidden[b * PKR_ENC_VISITED_HIDDEN + c]; return; }
    c -= PKR_ENC_VISITED_HIDDEN;
    if (c < PKR_ENC_BATTLE_HIDDEN) { out[idx] = battle_hidden[b * PKR_ENC_BATTLE_HIDDEN + c]; return; }
    c -= PKR_ENC_BATTLE_HIDDEN;
    if (c < PKR_ENC_PARTY_HIDDEN) { out[idx] = party_hidden[b * PKR_ENC_PARTY_HIDDEN + c]; return; }
    c -= PKR_ENC_PARTY_HIDDEN;
    out[idx] = bag_hidden[b * PKR_ENC_BAG_HIDDEN + c];
}

// Inverse of pkr_concat_kernel: routes the fused gradient back to each branch.
__global__ void pkr_split_grad_kernel(
        const precision_t* __restrict__ grad_concat, precision_t* __restrict__ conv_grad,
        precision_t* __restrict__ visited_grad, precision_t* __restrict__ battle_grad,
        precision_t* __restrict__ party_grad, precision_t* __restrict__ bag_grad, int B) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= B * PKR_ENC_CONCAT) {
        return;
    }
    int b = idx / PKR_ENC_CONCAT;
    int c = idx % PKR_ENC_CONCAT;
    precision_t g = grad_concat[idx];
    if (c < PKR_ENC_CONV_FLAT) { conv_grad[b * PKR_ENC_CONV_FLAT + c] = g; return; }
    c -= PKR_ENC_CONV_FLAT;
    if (c < PKR_ENC_VISITED_HIDDEN) { visited_grad[b * PKR_ENC_VISITED_HIDDEN + c] = g; return; }
    c -= PKR_ENC_VISITED_HIDDEN;
    if (c < PKR_ENC_BATTLE_HIDDEN) { battle_grad[b * PKR_ENC_BATTLE_HIDDEN + c] = g; return; }
    c -= PKR_ENC_BATTLE_HIDDEN;
    if (c < PKR_ENC_PARTY_HIDDEN) { party_grad[b * PKR_ENC_PARTY_HIDDEN + c] = g; return; }
    c -= PKR_ENC_PARTY_HIDDEN;
    bag_grad[b * PKR_ENC_BAG_HIDDEN + c] = g;
}

// Species table gradient: every party slot and both battle mons read it.
__global__ void pkr_species_embed_wgrad_kernel(
        precision_t* __restrict__ wgrad, const precision_t* __restrict__ grad_party_in,
        const precision_t* __restrict__ grad_battle_in, const int* __restrict__ species_idx, int B) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= PKR_SPECIES_VOCAB * PKR_SPECIES_EMBED_DIM) {
        return;
    }
    int v = idx / PKR_SPECIES_EMBED_DIM;
    int e = idx % PKR_SPECIES_EMBED_DIM;
    float sum = 0.0f;
    for (int b = 0; b < B; b++) {
        for (int s = 0; s < PKR_MON_SLOTS; s++) {
            if (species_idx[b * PKR_MON_SLOTS + s] != v) {
                continue;
            }
            if (s < PARTY_SIZE) {
                sum += to_float(grad_party_in[(b * PARTY_SIZE + s) * PKR_MON_FEAT + e]);
            } else {
                sum += to_float(grad_battle_in[b * PKR_ENC_BATTLE_IN + BATTLE_TYPE_COUNT
                    + (s - PARTY_SIZE) * PKR_MON_FEAT + e]);
            }
        }
    }
    wgrad[idx] = from_float(sum);
}

__global__ void pkr_item_embed_wgrad_kernel(
        precision_t* __restrict__ wgrad, const precision_t* __restrict__ grad_bag_in,
        const int* __restrict__ item_idx, int B) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= PKR_ITEM_VOCAB * PKR_ITEM_EMBED_DIM) {
        return;
    }
    int v = idx / PKR_ITEM_EMBED_DIM;
    int e = idx % PKR_ITEM_EMBED_DIM;
    float sum = 0.0f;
    for (int b = 0; b < B; b++) {
        for (int s = 0; s < BAG_SLOTS; s++) {
            if (item_idx[b * BAG_SLOTS + s] == v) {
                sum += to_float(grad_bag_in[(b * BAG_SLOTS + s) * PKR_BAG_SLOT_FEAT + e]);
            }
        }
    }
    wgrad[idx] = from_float(sum);
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
    int obs_size, hidden;
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
};

static void pkr_conv_wgrad(
        precision_t* grad_output_nchw, precision_t* wgrad,
        precision_t* col_buf, precision_t* mm_buf,
        int B, int OC, int spatial, int col_cols, cudaStream_t stream) {
    int col_rows = B * spatial;
    pkr_nchw_to_rows<<<grid_size(B * OC * spatial), BLOCK_SIZE, 0, stream>>>(
        grad_output_nchw, mm_buf, B, OC, spatial);
    Prec mm_t = {.data = mm_buf, .shape = {col_rows, OC}};
    Prec col_t = {.data = col_buf, .shape = {col_rows, col_cols}};
    Prec wg_t = {.data = wgrad, .shape = {OC, col_cols}};
    puf_mm_tn(&mm_t, &col_t, &wg_t, stream);
}

// Linear + ReLU over an already-built branch input.
static void pkr_branch_forward(
        Prec* branch_in, Prec* branch_w, Prec* branch_out, int hidden, int B, cudaStream_t stream) {
    puf_mm(branch_in, branch_w, branch_out, stream);
    pkr_relu_kernel<<<grid_size(B * hidden), BLOCK_SIZE, 0, stream>>>(branch_out->data, B * hidden);
}

// Backward of pkr_branch_forward. With branch_w set, the input gradient is written over
// branch_in (the embedding branches need it); the visited branch passes NULL.
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

static Prec pokered_encoder_forward(
        void* w, void* activations, Prec input, cudaStream_t stream) {
    PokeredEncoderWeights* ew = (PokeredEncoderWeights*)w;
    PokeredEncoderActivations* a = (PokeredEncoderActivations*)activations;
    int B = input.shape[0];

    int c1_rows = B * PKR_ENC_C1_SPATIAL;
    pkr_c1_im2col<<<grid_size(c1_rows * PKR_ENC_C1_COL_W), BLOCK_SIZE, 0, stream>>>(
        input.data, a->col1.data, B, ew->obs_size);
    Prec c1_col = {.data = a->col1.data, .shape = {c1_rows, PKR_ENC_C1_COL_W}};
    Prec c1_mm = {.data = a->mm1.data, .shape = {c1_rows, PKR_ENC_C1_OC}};
    puf_mm(&c1_col, &ew->conv1_w, &c1_mm, stream);
    pkr_rows_to_nchw<<<grid_size(B * PKR_ENC_C1_OC * PKR_ENC_C1_SPATIAL), BLOCK_SIZE, 0,
        stream>>>(a->mm1.data, a->conv1_out.data, B, PKR_ENC_C1_OC, PKR_ENC_C1_SPATIAL, 1);

    int c2_rows = B * PKR_ENC_C2_SPATIAL;
    pkr_c2_im2col<<<grid_size(c2_rows * PKR_ENC_C2_COL_W), BLOCK_SIZE, 0, stream>>>(
        a->conv1_out.data, a->col2.data, B);
    Prec c2_col = {.data = a->col2.data, .shape = {c2_rows, PKR_ENC_C2_COL_W}};
    Prec c2_mm = {.data = a->mm2.data, .shape = {c2_rows, PKR_ENC_C2_OC}};
    puf_mm(&c2_col, &ew->conv2_w, &c2_mm, stream);
    pkr_rows_to_nchw<<<grid_size(B * PKR_ENC_C2_OC * PKR_ENC_C2_SPATIAL), BLOCK_SIZE, 0,
        stream>>>(a->mm2.data, a->conv2_out.data, B, PKR_ENC_C2_OC, PKR_ENC_C2_SPATIAL, 0);

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

    pkr_concat_kernel<<<grid_size(B * PKR_ENC_CONCAT), BLOCK_SIZE, 0, stream>>>(
        a->concat.data, a->conv2_out.data, a->visited_out.data, a->battle_out.data,
        a->party_out.data, a->bag_out.data, B);

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
    Prec grad_concat = {.data = a->concat.data, .shape = {B, PKR_ENC_CONCAT}};
    puf_mm_nn(&grad, &ew->proj_w, &grad_concat, stream);

    pkr_split_grad_kernel<<<grid_size(B * PKR_ENC_CONCAT), BLOCK_SIZE, 0, stream>>>(
        grad_concat.data, a->conv2_grad.data, a->visited_grad.data, a->battle_grad.data,
        a->party_grad.data, a->bag_grad.data, B);

    pkr_branch_backward(&a->visited_grad, &a->visited_out, &a->visited_in, &a->visited_wgrad,
        NULL, PKR_ENC_VISITED_HIDDEN, stream);
    pkr_branch_backward(&a->battle_grad, &a->battle_out, &a->battle_in, &a->battle_wgrad,
        &ew->battle_w, PKR_ENC_BATTLE_HIDDEN, stream);
    pkr_branch_backward(&a->party_grad, &a->party_out, &a->party_in, &a->party_wgrad,
        &ew->party_w, PKR_ENC_PARTY_HIDDEN, stream);
    pkr_branch_backward(&a->bag_grad, &a->bag_out, &a->bag_in, &a->bag_wgrad,
        &ew->bag_w, PKR_ENC_BAG_HIDDEN, stream);

    // battle_in / party_in / bag_in now hold the gradient w.r.t. their inputs.
    pkr_species_embed_wgrad_kernel<<<grid_size(PKR_SPECIES_VOCAB * PKR_SPECIES_EMBED_DIM),
        BLOCK_SIZE, 0, stream>>>(a->species_embed_wgrad.data, a->party_in.data,
        a->battle_in.data, a->species_idx.data, B);
    pkr_item_embed_wgrad_kernel<<<grid_size(PKR_ITEM_VOCAB * PKR_ITEM_EMBED_DIM),
        BLOCK_SIZE, 0, stream>>>(a->item_embed_wgrad.data, a->bag_in.data, a->item_idx.data, B);

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
    Prec c1 = {.data = ew->conv1_w.data, .shape = {PKR_ENC_C1_OC, PKR_ENC_C1_COL_W}};
    Prec c2 = {.data = ew->conv2_w.data, .shape = {PKR_ENC_C2_OC, PKR_ENC_C2_COL_W}};
    Prec vis_w = {.data = ew->visited_w.data, .shape = {PKR_ENC_VISITED_HIDDEN, PKR_ENC_VISITED_IN}};
    Prec bat_w = {.data = ew->battle_w.data, .shape = {PKR_ENC_BATTLE_HIDDEN, PKR_ENC_BATTLE_IN}};
    Prec pty_w = {.data = ew->party_w.data, .shape = {PKR_ENC_PARTY_HIDDEN, PKR_ENC_PARTY_IN}};
    Prec bag_w = {.data = ew->bag_w.data, .shape = {PKR_ENC_BAG_HIDDEN, PKR_ENC_BAG_IN}};
    Prec proj = {.data = ew->proj_w.data, .shape = {ew->hidden, PKR_ENC_CONCAT}};
    puf_kaiming_init(&c1, sqrtf(2.0f), (*seed)++, stream);
    puf_kaiming_init(&c2, sqrtf(2.0f), (*seed)++, stream);
    puf_kaiming_init(&vis_w, sqrtf(2.0f), (*seed)++, stream);
    puf_kaiming_init(&bat_w, sqrtf(2.0f), (*seed)++, stream);
    puf_kaiming_init(&pty_w, sqrtf(2.0f), (*seed)++, stream);
    puf_kaiming_init(&bag_w, sqrtf(2.0f), (*seed)++, stream);
    puf_normal_init(&ew->species_embed_w, 0.02f, (*seed)++, stream);
    puf_normal_init(&ew->item_embed_w, 0.02f, (*seed)++, stream);
    puf_kaiming_init(&proj, sqrtf(2.0f), (*seed)++, stream);
}

static void pokered_encoder_reg_params(void* w, Allocator* alloc) {
    PokeredEncoderWeights* ew = (PokeredEncoderWeights*)w;
    ew->conv1_w = {.shape = {PKR_ENC_C1_OC, PKR_ENC_C1_COL_W}};
    ew->conv2_w = {.shape = {PKR_ENC_C2_OC, PKR_ENC_C2_COL_W}};
    ew->visited_w = {.shape = {PKR_ENC_VISITED_HIDDEN, PKR_ENC_VISITED_IN}};
    ew->battle_w = {.shape = {PKR_ENC_BATTLE_HIDDEN, PKR_ENC_BATTLE_IN}};
    ew->party_w = {.shape = {PKR_ENC_PARTY_HIDDEN, PKR_ENC_PARTY_IN}};
    ew->bag_w = {.shape = {PKR_ENC_BAG_HIDDEN, PKR_ENC_BAG_IN}};
    ew->species_embed_w = {.shape = {PKR_SPECIES_VOCAB, PKR_SPECIES_EMBED_DIM}};
    ew->item_embed_w = {.shape = {PKR_ITEM_VOCAB, PKR_ITEM_EMBED_DIM}};
    ew->proj_w = {.shape = {ew->hidden, PKR_ENC_CONCAT}};
    alloc_register(alloc, &ew->conv1_w);
    alloc_register(alloc, &ew->conv2_w);
    alloc_register(alloc, &ew->visited_w);
    alloc_register(alloc, &ew->battle_w);
    alloc_register(alloc, &ew->party_w);
    alloc_register(alloc, &ew->bag_w);
    alloc_register(alloc, &ew->species_embed_w);
    alloc_register(alloc, &ew->item_embed_w);
    alloc_register(alloc, &ew->proj_w);
}

static void pokered_encoder_reg_train(
        void* w, void* activations, Allocator* acts, Allocator* grads, int B_TT) {
    PokeredEncoderWeights* ew = (PokeredEncoderWeights*)w;
    PokeredEncoderActivations* a = (PokeredEncoderActivations*)activations;
    a->conv1_out = {.shape = {B_TT * PKR_ENC_C1_OC * PKR_ENC_C1_SPATIAL}};
    a->conv1_grad = {.shape = {B_TT * PKR_ENC_C1_OC * PKR_ENC_C1_SPATIAL}};
    a->conv1_wgrad = {.shape = {PKR_ENC_C1_OC, PKR_ENC_C1_COL_W}};
    a->col1 = {.shape = {B_TT * PKR_ENC_C1_SPATIAL, PKR_ENC_C1_COL_W}};
    a->mm1 = {.shape = {B_TT * PKR_ENC_C1_SPATIAL, PKR_ENC_C1_OC}};
    a->conv2_out = {.shape = {B_TT * PKR_ENC_C2_OC * PKR_ENC_C2_SPATIAL}};
    a->conv2_grad = {.shape = {B_TT * PKR_ENC_C2_OC * PKR_ENC_C2_SPATIAL}};
    a->conv2_wgrad = {.shape = {PKR_ENC_C2_OC, PKR_ENC_C2_COL_W}};
    a->col2 = {.shape = {B_TT * PKR_ENC_C2_SPATIAL, PKR_ENC_C2_COL_W}};
    a->mm2 = {.shape = {B_TT * PKR_ENC_C2_SPATIAL, PKR_ENC_C2_OC}};

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

    a->concat = {.shape = {B_TT, PKR_ENC_CONCAT}};
    a->out = {.shape = {B_TT, ew->hidden}};
    a->proj_wgrad = {.shape = {ew->hidden, PKR_ENC_CONCAT}};

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
    alloc_register(grads, &a->species_embed_wgrad);
    alloc_register(grads, &a->item_embed_wgrad);

    alloc_register(acts, &a->concat);
    alloc_register(acts, &a->out);
    alloc_register(grads, &a->proj_wgrad);
}

static void pokered_encoder_reg_rollout(
        void* w, void* activations, Allocator* alloc, int B) {
    PokeredEncoderWeights* ew = (PokeredEncoderWeights*)w;
    PokeredEncoderActivations* a = (PokeredEncoderActivations*)activations;
    a->conv1_out = {.shape = {B * PKR_ENC_C1_OC * PKR_ENC_C1_SPATIAL}};
    a->col1 = {.shape = {B * PKR_ENC_C1_SPATIAL, PKR_ENC_C1_COL_W}};
    a->mm1 = {.shape = {B * PKR_ENC_C1_SPATIAL, PKR_ENC_C1_OC}};
    a->conv2_out = {.shape = {B * PKR_ENC_C2_OC * PKR_ENC_C2_SPATIAL}};
    a->col2 = {.shape = {B * PKR_ENC_C2_SPATIAL, PKR_ENC_C2_COL_W}};
    a->mm2 = {.shape = {B * PKR_ENC_C2_SPATIAL, PKR_ENC_C2_OC}};

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

    a->concat = {.shape = {B, PKR_ENC_CONCAT}};
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
}

static void* pokered_encoder_create_weights(void* self) {
    Encoder* e = (Encoder*)self;
    PokeredEncoderWeights* ew = (PokeredEncoderWeights*)calloc(
        1, sizeof(PokeredEncoderWeights));
    ew->obs_size = e->in_dim;
    ew->hidden = e->out_dim;
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
