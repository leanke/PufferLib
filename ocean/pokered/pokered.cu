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

#define PKR_ENC_POSITION_IN POSITION_SCALAR_OBS
#define PKR_ENC_POSITION_HIDDEN 8

#define PKR_ENC_BATTLE_IN BATTLE_OBS
#define PKR_ENC_BATTLE_HIDDEN 24

#define PKR_ENC_PROGRESS_IN PROGRESS_OBS
#define PKR_ENC_PROGRESS_HIDDEN 24

#define PKR_ENC_VISITED_IN VISITED_MASK_OBS
#define PKR_ENC_VISITED_HIDDEN 8

#define PKR_PARTY_SPECIES_VOCAB 256
#define PKR_PARTY_SPECIES_EMBED_DIM 8
#define PKR_MOVE_VOCAB 256
#define PKR_MOVE_EMBED_DIM 4
#define PKR_PARTY_LEVEL_SCALE 100.0f
#define PKR_PARTY_HP_SCALE 1024.0f
#define PKR_PARTY_RAW_SCALARS 3
#define PKR_PARTY_SLOT_IN (PKR_PARTY_SPECIES_EMBED_DIM + 4 * PKR_MOVE_EMBED_DIM + \
    PKR_PARTY_RAW_SCALARS)
#define PKR_PARTY_MLP_IN (PARTY_SIZE * PKR_PARTY_SLOT_IN)
#define PKR_PARTY_HIDDEN 56

#define PKR_GATE_BRANCHES 5

#define PKR_ENC_CONCAT (PKR_ENC_CONV_FLAT + PKR_ENC_POSITION_HIDDEN + PKR_ENC_BATTLE_HIDDEN + \
    PKR_ENC_PROGRESS_HIDDEN + PKR_PARTY_HIDDEN + PKR_ENC_VISITED_HIDDEN)

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

__global__ void pkr_party_species_kernel(
        const precision_t* __restrict__ obs, int* __restrict__ species_idx,
        int B, int obs_size) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= B * PARTY_SIZE) {
        return;
    }
    int b = idx / PARTY_SIZE;
    int s = idx % PARTY_SIZE;
    float id = to_float(obs[(int64_t)b * obs_size + PARTY_OBS_OFFSET
        + s * PARTY_FIELDS + 0]);
    int v = (int)(id + 0.5f);
    v = v < 0 ? 0 : v;
    species_idx[idx] = v >= PKR_PARTY_SPECIES_VOCAB ? PKR_PARTY_SPECIES_VOCAB - 1 : v;
}

__global__ void pkr_party_move_idx_kernel(
        const precision_t* __restrict__ obs, int* __restrict__ move_idx,
        int B, int obs_size) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= B * PARTY_SIZE * 4) {
        return;
    }
    int b = idx / (PARTY_SIZE * 4);
    int rem = idx % (PARTY_SIZE * 4);
    int s = rem / 4;
    int mslot = rem % 4;
    float mv = to_float(obs[(int64_t)b * obs_size + PARTY_OBS_OFFSET
        + s * PARTY_FIELDS + 4 + mslot]);
    int v = (int)(mv + 0.5f);
    v = v < 0 ? 0 : v;
    move_idx[idx] = v >= PKR_MOVE_VOCAB ? PKR_MOVE_VOCAB - 1 : v;
}

__global__ void pkr_party_gather_kernel(
        const precision_t* __restrict__ obs,
        const precision_t* __restrict__ species_embed_w,
        const precision_t* __restrict__ move_embed_w,
        const int* __restrict__ species_idx, const int* __restrict__ move_idx,
        precision_t* __restrict__ out, int B, int obs_size) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= B * PKR_PARTY_MLP_IN) {
        return;
    }
    int b = idx / PKR_PARTY_MLP_IN;
    int rem = idx % PKR_PARTY_MLP_IN;
    int s = rem / PKR_PARTY_SLOT_IN;
    int f = rem % PKR_PARTY_SLOT_IN;

    if (f < PKR_PARTY_SPECIES_EMBED_DIM) {
        int v = species_idx[b * PARTY_SIZE + s];
        out[idx] = species_embed_w[v * PKR_PARTY_SPECIES_EMBED_DIM + f];
        return;
    }
    int f2 = f - PKR_PARTY_SPECIES_EMBED_DIM;
    if (f2 < 4 * PKR_MOVE_EMBED_DIM) {
        int mslot = f2 / PKR_MOVE_EMBED_DIM;
        int e = f2 % PKR_MOVE_EMBED_DIM;
        int v = move_idx[(b * PARTY_SIZE + s) * 4 + mslot];
        out[idx] = move_embed_w[v * PKR_MOVE_EMBED_DIM + e];
        return;
    }
    int field3 = f2 - 4 * PKR_MOVE_EMBED_DIM;
    int obs_field = 1 + field3;
    float scale = field3 == 0 ? PKR_PARTY_LEVEL_SCALE : PKR_PARTY_HP_SCALE;
    float raw = to_float(obs[(int64_t)b * obs_size + PARTY_OBS_OFFSET
        + s * PARTY_FIELDS + obs_field]);
    out[idx] = from_float(raw / scale);
}

__global__ void pkr_concat_kernel(
        precision_t* __restrict__ out, const precision_t* __restrict__ conv_flat,
        const precision_t* __restrict__ position_hidden,
        const precision_t* __restrict__ battle_hidden,
        const precision_t* __restrict__ progress_hidden,
        const precision_t* __restrict__ party_hidden,
        const precision_t* __restrict__ visited_hidden, int B) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= B * PKR_ENC_CONCAT) {
        return;
    }
    int b = idx / PKR_ENC_CONCAT;
    int c = idx % PKR_ENC_CONCAT;
    if (c < PKR_ENC_CONV_FLAT) { out[idx] = conv_flat[b * PKR_ENC_CONV_FLAT + c]; return; }
    c -= PKR_ENC_CONV_FLAT;
    if (c < PKR_ENC_POSITION_HIDDEN) { out[idx] = position_hidden[b * PKR_ENC_POSITION_HIDDEN + c]; return; }
    c -= PKR_ENC_POSITION_HIDDEN;
    if (c < PKR_ENC_BATTLE_HIDDEN) { out[idx] = battle_hidden[b * PKR_ENC_BATTLE_HIDDEN + c]; return; }
    c -= PKR_ENC_BATTLE_HIDDEN;
    if (c < PKR_ENC_PROGRESS_HIDDEN) { out[idx] = progress_hidden[b * PKR_ENC_PROGRESS_HIDDEN + c]; return; }
    c -= PKR_ENC_PROGRESS_HIDDEN;
    if (c < PKR_PARTY_HIDDEN) { out[idx] = party_hidden[b * PKR_PARTY_HIDDEN + c]; return; }
    c -= PKR_PARTY_HIDDEN;
    out[idx] = visited_hidden[b * PKR_ENC_VISITED_HIDDEN + c];
}

__device__ __forceinline__ int pkr_gate_branch_of(int off) {
    if (off < PKR_ENC_POSITION_HIDDEN) return 0;
    off -= PKR_ENC_POSITION_HIDDEN;
    if (off < PKR_ENC_BATTLE_HIDDEN) return 1;
    off -= PKR_ENC_BATTLE_HIDDEN;
    if (off < PKR_ENC_PROGRESS_HIDDEN) return 2;
    off -= PKR_ENC_PROGRESS_HIDDEN;
    if (off < PKR_PARTY_HIDDEN) return 3;
    off -= PKR_PARTY_HIDDEN;
    return 4;
}

__global__ void pkr_gate_kernel(
        const precision_t* __restrict__ obs, const precision_t* __restrict__ gate_w,
        const precision_t* __restrict__ gate_b, precision_t* __restrict__ gate_out,
        precision_t* __restrict__ gate_x, int B, int obs_size) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= B * PKR_GATE_BRANCHES) {
        return;
    }
    int b = idx / PKR_GATE_BRANCHES;
    int c = idx % PKR_GATE_BRANCHES;
    float x = to_float(obs[(int64_t)b * obs_size + BATTLE_OBS_OFFSET + 0]);
    if (c == 0) {
        gate_x[b] = from_float(x);
    }
    float z = to_float(gate_w[c]) * x + to_float(gate_b[c]);
    gate_out[idx] = from_float(sigmoid(z));
}

__global__ void pkr_gate_apply_kernel(
        precision_t* __restrict__ concat_gated, const precision_t* __restrict__ concat_raw,
        const precision_t* __restrict__ gate, int B) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= B * PKR_ENC_CONCAT) {
        return;
    }
    int b = idx / PKR_ENC_CONCAT;
    int c = idx % PKR_ENC_CONCAT;
    if (c < PKR_ENC_CONV_FLAT) { concat_gated[idx] = concat_raw[idx]; return; }
    int branch = pkr_gate_branch_of(c - PKR_ENC_CONV_FLAT);
    float g = to_float(gate[b * PKR_GATE_BRANCHES + branch]);
    concat_gated[idx] = from_float(to_float(concat_raw[idx]) * g);
}

__global__ void pkr_gate_copy_conv_grad_kernel(
        const precision_t* __restrict__ grad_concat_gated,
        precision_t* __restrict__ conv_grad, int B) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= B * PKR_ENC_CONV_FLAT) {
        return;
    }
    int b = idx / PKR_ENC_CONV_FLAT;
    int c = idx % PKR_ENC_CONV_FLAT;
    conv_grad[idx] = grad_concat_gated[b * PKR_ENC_CONCAT + c];
}

__global__ void pkr_gate_backward_kernel(
        const precision_t* __restrict__ grad_concat_gated,
        const precision_t* __restrict__ concat_raw,
        const precision_t* __restrict__ gate,
        precision_t* __restrict__ position_grad,
        precision_t* __restrict__ battle_grad,
        precision_t* __restrict__ progress_grad,
        precision_t* __restrict__ party_grad,
        precision_t* __restrict__ visited_grad,
        precision_t* __restrict__ gate_dz,
        int B) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= B * PKR_GATE_BRANCHES) {
        return;
    }
    int b = idx / PKR_GATE_BRANCHES;
    int c = idx % PKR_GATE_BRANCHES;
    int off0, len;
    precision_t* branch_grad;
    switch (c) {
        case 0: off0 = 0; len = PKR_ENC_POSITION_HIDDEN; branch_grad = position_grad; break;
        case 1: off0 = PKR_ENC_POSITION_HIDDEN; len = PKR_ENC_BATTLE_HIDDEN; branch_grad = battle_grad; break;
        case 2: off0 = PKR_ENC_POSITION_HIDDEN + PKR_ENC_BATTLE_HIDDEN;
                len = PKR_ENC_PROGRESS_HIDDEN; branch_grad = progress_grad; break;
        case 3: off0 = PKR_ENC_POSITION_HIDDEN + PKR_ENC_BATTLE_HIDDEN + PKR_ENC_PROGRESS_HIDDEN;
                len = PKR_PARTY_HIDDEN; branch_grad = party_grad; break;
        default: off0 = PKR_ENC_POSITION_HIDDEN + PKR_ENC_BATTLE_HIDDEN + PKR_ENC_PROGRESS_HIDDEN +
                PKR_PARTY_HIDDEN;
                len = PKR_ENC_VISITED_HIDDEN; branch_grad = visited_grad; break;
    }
    float g = to_float(gate[idx]);
    float dsum = 0.0f;
    for (int i = 0; i < len; i++) {
        int gi = b * PKR_ENC_CONCAT + PKR_ENC_CONV_FLAT + off0 + i;
        float gc = to_float(grad_concat_gated[gi]);
        float craw = to_float(concat_raw[gi]);
        branch_grad[b * len + i] = from_float(gc * g);
        dsum += gc * craw;
    }
    gate_dz[idx] = from_float(dsum * g * (1.0f - g));
}

__global__ void pkr_gate_wgrad_kernel(
        const precision_t* __restrict__ gate_dz, const precision_t* __restrict__ gate_x,
        precision_t* __restrict__ gate_w_grad, precision_t* __restrict__ gate_b_grad,
        int B) {
    int c = blockIdx.x * blockDim.x + threadIdx.x;
    if (c >= PKR_GATE_BRANCHES) {
        return;
    }
    float wg = 0.0f, bg = 0.0f;
    for (int b = 0; b < B; b++) {
        float dz = to_float(gate_dz[b * PKR_GATE_BRANCHES + c]);
        float x = to_float(gate_x[b]);
        wg += dz * x;
        bg += dz;
    }
    gate_w_grad[c] = from_float(wg);
    gate_b_grad[c] = from_float(bg);
}

__global__ void pkr_party_species_embed_wgrad_kernel(
        precision_t* __restrict__ wgrad, const precision_t* __restrict__ grad_party_in,
        const int* __restrict__ species_idx, int B) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= PKR_PARTY_SPECIES_VOCAB * PKR_PARTY_SPECIES_EMBED_DIM) {
        return;
    }
    int v = idx / PKR_PARTY_SPECIES_EMBED_DIM;
    int e = idx % PKR_PARTY_SPECIES_EMBED_DIM;
    float sum = 0.0f;
    for (int b = 0; b < B; b++) {
        for (int s = 0; s < PARTY_SIZE; s++) {
            if (species_idx[b * PARTY_SIZE + s] == v) {
                sum += to_float(grad_party_in[(b * PARTY_SIZE + s) * PKR_PARTY_SLOT_IN + e]);
            }
        }
    }
    wgrad[idx] = from_float(sum);
}

__global__ void pkr_party_move_embed_wgrad_kernel(
        precision_t* __restrict__ wgrad, const precision_t* __restrict__ grad_party_in,
        const int* __restrict__ move_idx, int B) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= PKR_MOVE_VOCAB * PKR_MOVE_EMBED_DIM) {
        return;
    }
    int v = idx / PKR_MOVE_EMBED_DIM;
    int e = idx % PKR_MOVE_EMBED_DIM;
    float sum = 0.0f;
    for (int b = 0; b < B; b++) {
        for (int s = 0; s < PARTY_SIZE; s++) {
            for (int mslot = 0; mslot < 4; mslot++) {
                if (move_idx[(b * PARTY_SIZE + s) * 4 + mslot] == v) {
                    int f = PKR_PARTY_SPECIES_EMBED_DIM + mslot * PKR_MOVE_EMBED_DIM + e;
                    sum += to_float(grad_party_in[(b * PARTY_SIZE + s) * PKR_PARTY_SLOT_IN + f]);
                }
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
    Prec position_w, battle_w, progress_w, visited_w;
    Prec party_species_embed_w, party_move_embed_w, party_w;
    Prec gate_w, gate_b;
    Prec proj_w;
    int obs_size, hidden;
};

struct PokeredEncoderActivations {
    Prec conv1_out, conv1_grad, conv1_wgrad, col1, mm1;
    Prec conv2_out, conv2_grad, conv2_wgrad, col2, mm2;
    Prec position_in, position_out, position_grad, position_wgrad;
    Prec battle_in, battle_out, battle_grad, battle_wgrad;
    Prec progress_in, progress_out, progress_grad, progress_wgrad;
    Prec visited_in, visited_out, visited_grad, visited_wgrad;
    Int party_species_idx, party_move_idx;
    Prec party_in, party_out, party_grad, party_wgrad;
    Prec party_species_embed_wgrad, party_move_embed_wgrad;
    Prec gate_out, gate_x, gate_dz, gate_w_grad, gate_b_grad;
    Prec concat_raw, concat, out, proj_wgrad;
};

static PokeredEncoderActivations* pkr_enc_last = NULL;

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

static void pkr_branch_forward(
        Prec* obs, Prec* branch_in, Prec* branch_w, Prec* branch_out,
        int offset, int len, int hidden, int B, int obs_size, cudaStream_t stream) {
    pkr_gather_range_kernel<<<grid_size(B * len), BLOCK_SIZE, 0, stream>>>(
        obs->data, branch_in->data, B, obs_size, offset, len);
    puf_mm(branch_in, branch_w, branch_out, stream);
    pkr_relu_kernel<<<grid_size(B * hidden), BLOCK_SIZE, 0, stream>>>(branch_out->data, B * hidden);
}

static void pkr_branch_backward(
        Prec* branch_grad, Prec* branch_out, Prec* branch_in, Prec* branch_wgrad,
        int hidden, cudaStream_t stream) {
    pkr_relu_backward_kernel<<<grid_size(branch_grad->shape[0] * hidden), BLOCK_SIZE, 0, stream>>>(
        branch_grad->data, branch_out->data, branch_grad->shape[0] * hidden);
    puf_mm_tn(branch_grad, branch_in, branch_wgrad, stream);
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

    pkr_branch_forward(&input, &a->position_in, &ew->position_w, &a->position_out,
        POSITION_OBS_OFFSET, PKR_ENC_POSITION_IN, PKR_ENC_POSITION_HIDDEN, B, ew->obs_size, stream);
    pkr_branch_forward(&input, &a->battle_in, &ew->battle_w, &a->battle_out,
        BATTLE_OBS_OFFSET, PKR_ENC_BATTLE_IN, PKR_ENC_BATTLE_HIDDEN, B, ew->obs_size, stream);
    pkr_branch_forward(&input, &a->progress_in, &ew->progress_w, &a->progress_out,
        PROGRESS_OBS_OFFSET, PKR_ENC_PROGRESS_IN, PKR_ENC_PROGRESS_HIDDEN, B, ew->obs_size, stream);
    pkr_branch_forward(&input, &a->visited_in, &ew->visited_w, &a->visited_out,
        VISITED_MASK_OFFSET, PKR_ENC_VISITED_IN, PKR_ENC_VISITED_HIDDEN, B, ew->obs_size, stream);

    pkr_party_species_kernel<<<grid_size(B * PARTY_SIZE), BLOCK_SIZE, 0, stream>>>(
        input.data, a->party_species_idx.data, B, ew->obs_size);
    pkr_party_move_idx_kernel<<<grid_size(B * PARTY_SIZE * 4), BLOCK_SIZE, 0, stream>>>(
        input.data, a->party_move_idx.data, B, ew->obs_size);
    pkr_party_gather_kernel<<<grid_size(B * PKR_PARTY_MLP_IN), BLOCK_SIZE, 0, stream>>>(
        input.data, ew->party_species_embed_w.data, ew->party_move_embed_w.data,
        a->party_species_idx.data, a->party_move_idx.data,
        a->party_in.data, B, ew->obs_size);
    puf_mm(&a->party_in, &ew->party_w, &a->party_out, stream);
    pkr_relu_kernel<<<grid_size(B * PKR_PARTY_HIDDEN), BLOCK_SIZE, 0, stream>>>(
        a->party_out.data, B * PKR_PARTY_HIDDEN);

    pkr_concat_kernel<<<grid_size(B * PKR_ENC_CONCAT), BLOCK_SIZE, 0, stream>>>(
        a->concat_raw.data, a->conv2_out.data, a->position_out.data, a->battle_out.data,
        a->progress_out.data, a->party_out.data, a->visited_out.data, B);

    pkr_gate_kernel<<<grid_size(B * PKR_GATE_BRANCHES), BLOCK_SIZE, 0, stream>>>(
        input.data, ew->gate_w.data, ew->gate_b.data, a->gate_out.data, a->gate_x.data, B, ew->obs_size);
    pkr_gate_apply_kernel<<<grid_size(B * PKR_ENC_CONCAT), BLOCK_SIZE, 0, stream>>>(
        a->concat.data, a->concat_raw.data, a->gate_out.data, B);

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
    Prec grad_concat_gated = {.data = a->concat.data, .shape = {B, PKR_ENC_CONCAT}};
    puf_mm_nn(&grad, &ew->proj_w, &grad_concat_gated, stream);

    pkr_gate_copy_conv_grad_kernel<<<grid_size(B * PKR_ENC_CONV_FLAT), BLOCK_SIZE, 0,
        stream>>>(grad_concat_gated.data, a->conv2_grad.data, B);
    pkr_gate_backward_kernel<<<grid_size(B * PKR_GATE_BRANCHES), BLOCK_SIZE, 0, stream>>>(
        grad_concat_gated.data, a->concat_raw.data, a->gate_out.data,
        a->position_grad.data, a->battle_grad.data, a->progress_grad.data,
        a->party_grad.data, a->visited_grad.data, a->gate_dz.data, B);
    pkr_gate_wgrad_kernel<<<grid_size(PKR_GATE_BRANCHES), BLOCK_SIZE, 0, stream>>>(
        a->gate_dz.data, a->gate_x.data, a->gate_w_grad.data, a->gate_b_grad.data, B);

    pkr_branch_backward(&a->position_grad, &a->position_out, &a->position_in, &a->position_wgrad,
        PKR_ENC_POSITION_HIDDEN, stream);
    pkr_branch_backward(&a->battle_grad, &a->battle_out, &a->battle_in, &a->battle_wgrad,
        PKR_ENC_BATTLE_HIDDEN, stream);
    pkr_branch_backward(&a->progress_grad, &a->progress_out, &a->progress_in, &a->progress_wgrad,
        PKR_ENC_PROGRESS_HIDDEN, stream);
    pkr_branch_backward(&a->visited_grad, &a->visited_out, &a->visited_in, &a->visited_wgrad,
        PKR_ENC_VISITED_HIDDEN, stream);

    pkr_relu_backward_kernel<<<grid_size(B * PKR_PARTY_HIDDEN), BLOCK_SIZE, 0,
        stream>>>(a->party_grad.data, a->party_out.data, B * PKR_PARTY_HIDDEN);
    Prec party_grad_t = {.data = a->party_grad.data, .shape = {B, PKR_PARTY_HIDDEN}};
    puf_mm_tn(&party_grad_t, &a->party_in, &a->party_wgrad, stream);
    Prec grad_party_in = {.data = a->party_in.data, .shape = {B, PKR_PARTY_MLP_IN}};
    puf_mm_nn(&party_grad_t, &ew->party_w, &grad_party_in, stream);
    pkr_party_species_embed_wgrad_kernel<<<
        grid_size(PKR_PARTY_SPECIES_VOCAB * PKR_PARTY_SPECIES_EMBED_DIM), BLOCK_SIZE, 0,
        stream>>>(a->party_species_embed_wgrad.data, grad_party_in.data,
        a->party_species_idx.data, B);
    pkr_party_move_embed_wgrad_kernel<<<
        grid_size(PKR_MOVE_VOCAB * PKR_MOVE_EMBED_DIM), BLOCK_SIZE, 0,
        stream>>>(a->party_move_embed_wgrad.data, grad_party_in.data,
        a->party_move_idx.data, B);

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
    Prec pos_w = {.data = ew->position_w.data, .shape = {PKR_ENC_POSITION_HIDDEN, PKR_ENC_POSITION_IN}};
    Prec bat_w = {.data = ew->battle_w.data, .shape = {PKR_ENC_BATTLE_HIDDEN, PKR_ENC_BATTLE_IN}};
    Prec prog_w = {.data = ew->progress_w.data, .shape = {PKR_ENC_PROGRESS_HIDDEN, PKR_ENC_PROGRESS_IN}};
    Prec vis_w = {.data = ew->visited_w.data, .shape = {PKR_ENC_VISITED_HIDDEN, PKR_ENC_VISITED_IN}};
    Prec pw = {.data = ew->party_w.data, .shape = {PKR_PARTY_HIDDEN, PKR_PARTY_MLP_IN}};
    Prec proj = {.data = ew->proj_w.data, .shape = {ew->hidden, PKR_ENC_CONCAT}};
    puf_kaiming_init(&c1, sqrtf(2.0f), (*seed)++, stream);
    puf_kaiming_init(&c2, sqrtf(2.0f), (*seed)++, stream);
    puf_kaiming_init(&pos_w, sqrtf(2.0f), (*seed)++, stream);
    puf_kaiming_init(&bat_w, sqrtf(2.0f), (*seed)++, stream);
    puf_kaiming_init(&prog_w, sqrtf(2.0f), (*seed)++, stream);
    puf_kaiming_init(&vis_w, sqrtf(2.0f), (*seed)++, stream);
    puf_kaiming_init(&pw, sqrtf(2.0f), (*seed)++, stream);
    puf_normal_init(&ew->party_species_embed_w, 0.02f, (*seed)++, stream);
    puf_normal_init(&ew->party_move_embed_w, 0.02f, (*seed)++, stream);

    puf_normal_init(&ew->gate_w, 0.01f, (*seed)++, stream);
    puf_normal_init(&ew->gate_b, 0.01f, (*seed)++, stream);
    puf_kaiming_init(&proj, sqrtf(2.0f), (*seed)++, stream);
}

static void pokered_encoder_reg_params(void* w, Allocator* alloc) {
    PokeredEncoderWeights* ew = (PokeredEncoderWeights*)w;
    ew->conv1_w = {.shape = {PKR_ENC_C1_OC, PKR_ENC_C1_COL_W}};
    ew->conv2_w = {.shape = {PKR_ENC_C2_OC, PKR_ENC_C2_COL_W}};
    ew->position_w = {.shape = {PKR_ENC_POSITION_HIDDEN, PKR_ENC_POSITION_IN}};
    ew->battle_w = {.shape = {PKR_ENC_BATTLE_HIDDEN, PKR_ENC_BATTLE_IN}};
    ew->progress_w = {.shape = {PKR_ENC_PROGRESS_HIDDEN, PKR_ENC_PROGRESS_IN}};
    ew->visited_w = {.shape = {PKR_ENC_VISITED_HIDDEN, PKR_ENC_VISITED_IN}};
    ew->party_species_embed_w = {.shape = {PKR_PARTY_SPECIES_VOCAB, PKR_PARTY_SPECIES_EMBED_DIM}};
    ew->party_move_embed_w = {.shape = {PKR_MOVE_VOCAB, PKR_MOVE_EMBED_DIM}};
    ew->party_w = {.shape = {PKR_PARTY_HIDDEN, PKR_PARTY_MLP_IN}};
    ew->gate_w = {.shape = {PKR_GATE_BRANCHES}};
    ew->gate_b = {.shape = {PKR_GATE_BRANCHES}};
    ew->proj_w = {.shape = {ew->hidden, PKR_ENC_CONCAT}};
    alloc_register(alloc, &ew->conv1_w);
    alloc_register(alloc, &ew->conv2_w);
    alloc_register(alloc, &ew->position_w);
    alloc_register(alloc, &ew->battle_w);
    alloc_register(alloc, &ew->progress_w);
    alloc_register(alloc, &ew->visited_w);
    alloc_register(alloc, &ew->party_species_embed_w);
    alloc_register(alloc, &ew->party_move_embed_w);
    alloc_register(alloc, &ew->party_w);
    alloc_register(alloc, &ew->gate_w);
    alloc_register(alloc, &ew->gate_b);
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

    a->position_in = {.shape = {B_TT, PKR_ENC_POSITION_IN}};
    a->position_out = {.shape = {B_TT, PKR_ENC_POSITION_HIDDEN}};
    a->position_grad = {.shape = {B_TT, PKR_ENC_POSITION_HIDDEN}};
    a->position_wgrad = {.shape = {PKR_ENC_POSITION_HIDDEN, PKR_ENC_POSITION_IN}};
    a->battle_in = {.shape = {B_TT, PKR_ENC_BATTLE_IN}};
    a->battle_out = {.shape = {B_TT, PKR_ENC_BATTLE_HIDDEN}};
    a->battle_grad = {.shape = {B_TT, PKR_ENC_BATTLE_HIDDEN}};
    a->battle_wgrad = {.shape = {PKR_ENC_BATTLE_HIDDEN, PKR_ENC_BATTLE_IN}};
    a->progress_in = {.shape = {B_TT, PKR_ENC_PROGRESS_IN}};
    a->progress_out = {.shape = {B_TT, PKR_ENC_PROGRESS_HIDDEN}};
    a->progress_grad = {.shape = {B_TT, PKR_ENC_PROGRESS_HIDDEN}};
    a->progress_wgrad = {.shape = {PKR_ENC_PROGRESS_HIDDEN, PKR_ENC_PROGRESS_IN}};
    a->visited_in = {.shape = {B_TT, PKR_ENC_VISITED_IN}};
    a->visited_out = {.shape = {B_TT, PKR_ENC_VISITED_HIDDEN}};
    a->visited_grad = {.shape = {B_TT, PKR_ENC_VISITED_HIDDEN}};
    a->visited_wgrad = {.shape = {PKR_ENC_VISITED_HIDDEN, PKR_ENC_VISITED_IN}};

    a->party_species_idx = {.shape = {B_TT, PARTY_SIZE}};
    a->party_move_idx = {.shape = {B_TT, PARTY_SIZE, 4}};
    a->party_in = {.shape = {B_TT, PKR_PARTY_MLP_IN}};
    a->party_out = {.shape = {B_TT, PKR_PARTY_HIDDEN}};
    a->party_grad = {.shape = {B_TT, PKR_PARTY_HIDDEN}};
    a->party_wgrad = {.shape = {PKR_PARTY_HIDDEN, PKR_PARTY_MLP_IN}};
    a->party_species_embed_wgrad = {.shape = {PKR_PARTY_SPECIES_VOCAB, PKR_PARTY_SPECIES_EMBED_DIM}};
    a->party_move_embed_wgrad = {.shape = {PKR_MOVE_VOCAB, PKR_MOVE_EMBED_DIM}};

    a->gate_out = {.shape = {B_TT, PKR_GATE_BRANCHES}};
    a->gate_x = {.shape = {B_TT}};
    a->gate_dz = {.shape = {B_TT, PKR_GATE_BRANCHES}};
    a->gate_w_grad = {.shape = {PKR_GATE_BRANCHES}};
    a->gate_b_grad = {.shape = {PKR_GATE_BRANCHES}};

    a->concat_raw = {.shape = {B_TT, PKR_ENC_CONCAT}};
    a->concat = {.shape = {B_TT, PKR_ENC_CONCAT}};
    a->out = {.shape = {B_TT, ew->hidden}};
    a->proj_wgrad = {.shape = {ew->hidden, PKR_ENC_CONCAT}};

    alloc_register(acts, &a->conv1_out); alloc_register(acts, &a->conv1_grad);
    alloc_register(grads, &a->conv1_wgrad);
    alloc_register(acts, &a->col1); alloc_register(acts, &a->mm1);
    alloc_register(acts, &a->conv2_out); alloc_register(acts, &a->conv2_grad);
    alloc_register(grads, &a->conv2_wgrad);
    alloc_register(acts, &a->col2); alloc_register(acts, &a->mm2);

    alloc_register(acts, &a->position_in); alloc_register(acts, &a->position_out);
    alloc_register(acts, &a->position_grad); alloc_register(grads, &a->position_wgrad);
    alloc_register(acts, &a->battle_in); alloc_register(acts, &a->battle_out);
    alloc_register(acts, &a->battle_grad); alloc_register(grads, &a->battle_wgrad);
    alloc_register(acts, &a->progress_in); alloc_register(acts, &a->progress_out);
    alloc_register(acts, &a->progress_grad); alloc_register(grads, &a->progress_wgrad);
    alloc_register(acts, &a->visited_in); alloc_register(acts, &a->visited_out);
    alloc_register(acts, &a->visited_grad); alloc_register(grads, &a->visited_wgrad);

    alloc_register(acts, &a->party_species_idx); alloc_register(acts, &a->party_move_idx);
    alloc_register(acts, &a->party_in); alloc_register(acts, &a->party_out);
    alloc_register(acts, &a->party_grad); alloc_register(grads, &a->party_wgrad);
    alloc_register(grads, &a->party_species_embed_wgrad);
    alloc_register(grads, &a->party_move_embed_wgrad);

    alloc_register(acts, &a->gate_out); alloc_register(acts, &a->gate_x);
    alloc_register(acts, &a->gate_dz);
    alloc_register(grads, &a->gate_w_grad); alloc_register(grads, &a->gate_b_grad);

    alloc_register(acts, &a->concat_raw); alloc_register(acts, &a->concat);
    alloc_register(acts, &a->out);
    alloc_register(grads, &a->proj_wgrad);
    pkr_enc_last = a;
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

    a->position_in = {.shape = {B, PKR_ENC_POSITION_IN}};
    a->position_out = {.shape = {B, PKR_ENC_POSITION_HIDDEN}};
    a->battle_in = {.shape = {B, PKR_ENC_BATTLE_IN}};
    a->battle_out = {.shape = {B, PKR_ENC_BATTLE_HIDDEN}};
    a->progress_in = {.shape = {B, PKR_ENC_PROGRESS_IN}};
    a->progress_out = {.shape = {B, PKR_ENC_PROGRESS_HIDDEN}};
    a->visited_in = {.shape = {B, PKR_ENC_VISITED_IN}};
    a->visited_out = {.shape = {B, PKR_ENC_VISITED_HIDDEN}};

    a->party_species_idx = {.shape = {B, PARTY_SIZE}};
    a->party_move_idx = {.shape = {B, PARTY_SIZE, 4}};
    a->party_in = {.shape = {B, PKR_PARTY_MLP_IN}};
    a->party_out = {.shape = {B, PKR_PARTY_HIDDEN}};

    a->gate_out = {.shape = {B, PKR_GATE_BRANCHES}};
    a->gate_x = {.shape = {B}};
    a->concat_raw = {.shape = {B, PKR_ENC_CONCAT}};
    a->concat = {.shape = {B, PKR_ENC_CONCAT}};
    a->out = {.shape = {B, ew->hidden}};

    alloc_register(alloc, &a->conv1_out);
    alloc_register(alloc, &a->col1); alloc_register(alloc, &a->mm1);
    alloc_register(alloc, &a->conv2_out);
    alloc_register(alloc, &a->col2); alloc_register(alloc, &a->mm2);
    alloc_register(alloc, &a->position_in); alloc_register(alloc, &a->position_out);
    alloc_register(alloc, &a->battle_in); alloc_register(alloc, &a->battle_out);
    alloc_register(alloc, &a->progress_in); alloc_register(alloc, &a->progress_out);
    alloc_register(alloc, &a->visited_in); alloc_register(alloc, &a->visited_out);
    alloc_register(alloc, &a->party_species_idx); alloc_register(alloc, &a->party_move_idx);
    alloc_register(alloc, &a->party_in); alloc_register(alloc, &a->party_out);
    alloc_register(alloc, &a->gate_out); alloc_register(alloc, &a->gate_x);
    alloc_register(alloc, &a->concat_raw); alloc_register(alloc, &a->concat);
    alloc_register(alloc, &a->out);
    pkr_enc_last = a;
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

#ifdef POKERED_DUAL_HEAD

__global__ void pkr_decoder_select_kernel(
        precision_t* __restrict__ out, const precision_t* __restrict__ battle_out,
        const precision_t* __restrict__ overworld_out,
        const precision_t* __restrict__ gate_x, int B, int od1) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= B * od1) {
        return;
    }
    int b = idx / od1;
    bool in_battle = to_float(gate_x[b]) > 0.5f;
    out[idx] = in_battle ? battle_out[idx] : overworld_out[idx];
}

__global__ void pkr_decoder_route_grad_kernel(
        precision_t* __restrict__ grad_battle_out,
        precision_t* __restrict__ grad_overworld_out,
        const precision_t* __restrict__ grad_out,
        const precision_t* __restrict__ gate_x, int B, int od1) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= B * od1) {
        return;
    }
    int b = idx / od1;
    bool in_battle = to_float(gate_x[b]) > 0.5f;
    precision_t g = grad_out[idx];
    precision_t z = from_float(0.0f);
    grad_battle_out[idx] = in_battle ? g : z;
    grad_overworld_out[idx] = in_battle ? z : g;
}

struct PokeredDecoderWeights {

    Prec weight_unused, logstd;
    int hidden_dim, output_dim;
    bool continuous;
    Prec battle_weight, overworld_weight;
};

struct PokeredDecoderActivations {
    PokeredEncoderActivations* enc;
    Prec battle_out, overworld_out;
    Prec out;
    Prec saved_input, grad_input;
    Prec grad_out;
    Prec grad_battle_out, grad_overworld_out;
    Prec battle_wgrad, overworld_wgrad;
};

static_assert(offsetof(PokeredDecoderWeights, logstd) == offsetof(DecoderWeights, logstd),
              "PokeredDecoderWeights header must mirror DecoderWeights (logstd)");
static_assert(offsetof(PokeredDecoderWeights, continuous) == offsetof(DecoderWeights, continuous),
              "PokeredDecoderWeights header must mirror DecoderWeights (continuous)");

static Prec pokered_decoder_forward(void* w, void* activations, Prec input, cudaStream_t stream) {
    PokeredDecoderWeights* dw = (PokeredDecoderWeights*)w;
    PokeredDecoderActivations* a = (PokeredDecoderActivations*)activations;
    int B = input.shape[0];
    int od1 = dw->output_dim + 1;
    if (a->saved_input.data) {
        puf_copy(&a->saved_input, &input, stream);
    }
    puf_mm(&input, &dw->battle_weight, &a->battle_out, stream);
    puf_mm(&input, &dw->overworld_weight, &a->overworld_out, stream);
    PokeredEncoderActivations* ea = a->enc;
    pkr_decoder_select_kernel<<<grid_size(B * od1), BLOCK_SIZE, 0, stream>>>(
        a->out.data, a->battle_out.data, a->overworld_out.data, ea->gate_x.data, B, od1);
    return a->out;
}

static Prec pokered_decoder_backward(void* w, void* activations,
        Float grad_logits, Float grad_logstd, Float grad_value, cudaStream_t stream) {
    (void)grad_logstd;
    PokeredDecoderWeights* dw = (PokeredDecoderWeights*)w;
    PokeredDecoderActivations* a = (PokeredDecoderActivations*)activations;
    int B = a->saved_input.shape[0];
    int od = dw->output_dim, od1 = od + 1;
    assemble_decoder_grad<<<grid_size(B * od1), BLOCK_SIZE, 0, stream>>>(
        a->grad_out.data, grad_logits.data, grad_value.data, B, od, od1);
    PokeredEncoderActivations* ea = a->enc;
    pkr_decoder_route_grad_kernel<<<grid_size(B * od1), BLOCK_SIZE, 0, stream>>>(
        a->grad_battle_out.data, a->grad_overworld_out.data, a->grad_out.data,
        ea->gate_x.data, B, od1);

    puf_mm_tn_async_after(&a->grad_battle_out, &a->saved_input, &a->battle_wgrad, stream);
    puf_mm_tn_async_after(&a->grad_overworld_out, &a->saved_input, &a->overworld_wgrad, stream);
    puf_mm_nn(&a->grad_battle_out, &dw->battle_weight, &a->grad_input, stream, 1.0f, 0.0f);
    puf_mm_nn(&a->grad_overworld_out, &dw->overworld_weight, &a->grad_input, stream, 1.0f, 1.0f);
    return a->grad_input;
}

static void pokered_decoder_init_weights(void* w, uint64_t* seed, cudaStream_t stream) {
    PokeredDecoderWeights* dw = (PokeredDecoderWeights*)w;
    puf_kaiming_init(&dw->battle_weight, 1.0f, (*seed)++, stream);
    puf_kaiming_init(&dw->overworld_weight, 1.0f, (*seed)++, stream);
}

static void pokered_decoder_reg_params(void* w, Allocator* alloc) {
    PokeredDecoderWeights* dw = (PokeredDecoderWeights*)w;
    int od1 = dw->output_dim + 1;
    dw->battle_weight = {.shape = {od1, dw->hidden_dim}};
    dw->overworld_weight = {.shape = {od1, dw->hidden_dim}};
    alloc_register(alloc, &dw->battle_weight);
    alloc_register(alloc, &dw->overworld_weight);
}

static void pokered_decoder_reg_train(void* w, void* activations,
        Allocator* acts, Allocator* grads, int B_TT) {
    PokeredDecoderWeights* dw = (PokeredDecoderWeights*)w;
    PokeredDecoderActivations* a = (PokeredDecoderActivations*)activations;
    int od1 = dw->output_dim + 1;
    *a = {};
    a->battle_out = {.shape = {B_TT, od1}};
    a->overworld_out = {.shape = {B_TT, od1}};
    a->out = {.shape = {B_TT, od1}};
    a->saved_input = {.shape = {B_TT, dw->hidden_dim}};
    a->grad_input = {.shape = {B_TT, dw->hidden_dim}};
    a->grad_out = {.shape = {B_TT, od1}};
    a->grad_battle_out = {.shape = {B_TT, od1}};
    a->grad_overworld_out = {.shape = {B_TT, od1}};
    a->battle_wgrad = {.shape = {od1, dw->hidden_dim}};
    a->overworld_wgrad = {.shape = {od1, dw->hidden_dim}};
    alloc_register(acts, &a->battle_out); alloc_register(acts, &a->overworld_out);
    alloc_register(acts, &a->out); alloc_register(acts, &a->saved_input);
    alloc_register(acts, &a->grad_input); alloc_register(acts, &a->grad_out);
    alloc_register(acts, &a->grad_battle_out); alloc_register(acts, &a->grad_overworld_out);
    alloc_register(grads, &a->battle_wgrad); alloc_register(grads, &a->overworld_wgrad);
    a->enc = pkr_enc_last;
}

static void pokered_decoder_reg_rollout(void* w, void* activations, Allocator* alloc, int B) {
    PokeredDecoderWeights* dw = (PokeredDecoderWeights*)w;
    PokeredDecoderActivations* a = (PokeredDecoderActivations*)activations;
    int od1 = dw->output_dim + 1;
    *a = {};
    a->battle_out = {.shape = {B, od1}};
    a->overworld_out = {.shape = {B, od1}};
    a->out = {.shape = {B, od1}};
    alloc_register(alloc, &a->battle_out);
    alloc_register(alloc, &a->overworld_out);
    alloc_register(alloc, &a->out);
    a->enc = pkr_enc_last;
}

static void* pokered_decoder_create_weights(void* self) {
    Decoder* d = (Decoder*)self;
    PokeredDecoderWeights* dw = (PokeredDecoderWeights*)calloc(1, sizeof(PokeredDecoderWeights));
    dw->hidden_dim = d->hidden_dim;
    dw->output_dim = d->output_dim;
    dw->continuous = false;
    return dw;
}

static void create_pokered_decoder(Decoder* dec) {
    *dec = Decoder{
        .forward = pokered_decoder_forward,
        .backward = pokered_decoder_backward,
        .init_weights = pokered_decoder_init_weights,
        .reg_params = pokered_decoder_reg_params,
        .reg_train = pokered_decoder_reg_train,
        .reg_rollout = pokered_decoder_reg_rollout,
        .create_weights = pokered_decoder_create_weights,
        .hidden_dim = dec->hidden_dim, .output_dim = dec->output_dim,
        .continuous = dec->continuous,
        .activation_size = (int)sizeof(PokeredDecoderActivations),
    };
}
#endif
