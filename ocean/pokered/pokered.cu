/*
 * Pokered encoder network.
 *
 * Input observations are processed per batch row from three branches:
 *
 * 1. Screen branch: the 80x72 grayscale screen is normalized to [0, 1],
 *    transformed with im2col, then passed through a 1->32 convolution with
 *    an 8x8 kernel and stride 4. The resulting 17x19 feature map uses ReLU.
 *    A second 32->64 convolution uses a 3x3 kernel and stride 2, producing
 *    an 8x9 feature map without an activation. Its flattened output has
 *    64*8*9 = 4608 features.
 *
 * 2. Scalar branch: the six general scalar observations and the 15x15
 *    visited-coordinate window are gathered into 231 features, projected by
 *    a 231->32 linear layer, and passed through ReLU.
 *
 * 3. Party branch: each of the six party slots maps its categorical species
 *    byte through a learned 256x8 embedding table. The embedding is joined
 *    with normalized level, HP, and max HP values, giving 11 features per
 *    slot and 66 features for the party MLP. A 66->32 linear layer followed
 *    by ReLU produces the party representation.
 *
 * The three outputs are concatenated into 4608+32+32 = 4672 features and
 * projected by a 4672->hidden linear layer followed by ReLU. This output is
 * consumed by the shared MinGRU/decoder. Matrix multiplications use im2col
 * buffers and the common GPU matmul routines. Backward propagation reverses
 * these operations, computes gradients for all weights, and accumulates
 * embedding gradients one table row at a time.
 */

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

#define PKR_ENC_SCALAR_IN (GENERAL_SCALAR_OBS + VISITED_OBS)
#define PKR_ENC_SCALAR_HIDDEN 32

#define PKR_PARTY_SPECIES_VOCAB 256
#define PKR_PARTY_EMBED_DIM 8
#define PKR_PARTY_LEVEL_SCALE 100.0f
#define PKR_PARTY_HP_SCALE 1024.0f
#define PKR_PARTY_SLOT_IN (PKR_PARTY_EMBED_DIM + 3)
#define PKR_PARTY_MLP_IN (PARTY_SIZE * PKR_PARTY_SLOT_IN)
#define PKR_PARTY_HIDDEN 32

#define PKR_ENC_CONCAT (PKR_ENC_CONV_FLAT + PKR_ENC_SCALAR_HIDDEN + PKR_PARTY_HIDDEN)

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
    int kh = c / PKR_ENC_C1_K;
    int kw = c % PKR_ENC_C1_K;
    int ih = oh * PKR_ENC_C1_S + kh;
    int iw = ow * PKR_ENC_C1_S + kw;
    float pixel = to_float(obs[(int64_t)b * obs_size + ih * SCALED_WIDTH + iw]);
    col[idx] = from_float(pixel / 255.0f);
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

__global__ void pkr_gather_scalar_kernel(
        const precision_t* __restrict__ obs, precision_t* __restrict__ out,
        int B, int obs_size) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= B * PKR_ENC_SCALAR_IN) {
        return;
    }
    int b = idx / PKR_ENC_SCALAR_IN;
    int f = idx % PKR_ENC_SCALAR_IN;
    out[idx] = obs[(int64_t)b * obs_size + SCALED_PIXELS + f];
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

__global__ void pkr_party_gather_kernel(
        const precision_t* __restrict__ obs, const precision_t* __restrict__ embed_w,
        const int* __restrict__ species_idx, precision_t* __restrict__ out,
        int B, int obs_size) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= B * PKR_PARTY_MLP_IN) {
        return;
    }
    int b = idx / PKR_PARTY_MLP_IN;
    int rem = idx % PKR_PARTY_MLP_IN;
    int s = rem / PKR_PARTY_SLOT_IN;
    int f = rem % PKR_PARTY_SLOT_IN;
    if (f < PKR_PARTY_EMBED_DIM) {
        int v = species_idx[b * PARTY_SIZE + s];
        out[idx] = embed_w[v * PKR_PARTY_EMBED_DIM + f];
        return;
    }
    int field = 1 + (f - PKR_PARTY_EMBED_DIM);
    float raw = to_float(obs[(int64_t)b * obs_size + PARTY_OBS_OFFSET
        + s * PARTY_FIELDS + field]);
    float scale = (field == 1) ? PKR_PARTY_LEVEL_SCALE : PKR_PARTY_HP_SCALE;
    out[idx] = from_float(raw / scale);
}

__global__ void pkr_concat_kernel(
        precision_t* __restrict__ out, const precision_t* __restrict__ conv_flat,
        const precision_t* __restrict__ scalar_hidden,
        const precision_t* __restrict__ party_hidden, int B) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= B * PKR_ENC_CONCAT) {
        return;
    }
    int b = idx / PKR_ENC_CONCAT;
    int c = idx % PKR_ENC_CONCAT;
    if (c < PKR_ENC_CONV_FLAT) {
        out[idx] = conv_flat[b * PKR_ENC_CONV_FLAT + c];
    } else if (c < PKR_ENC_CONV_FLAT + PKR_ENC_SCALAR_HIDDEN) {
        out[idx] = scalar_hidden[b * PKR_ENC_SCALAR_HIDDEN + (c - PKR_ENC_CONV_FLAT)];
    } else {
        out[idx] = party_hidden[b * PKR_PARTY_HIDDEN
            + (c - PKR_ENC_CONV_FLAT - PKR_ENC_SCALAR_HIDDEN)];
    }
}

__global__ void pkr_concat_backward_conv_kernel(
        precision_t* __restrict__ conv_grad,
        const precision_t* __restrict__ concat_grad, int B) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= B * PKR_ENC_CONV_FLAT) {
        return;
    }
    int b = idx / PKR_ENC_CONV_FLAT;
    int c = idx % PKR_ENC_CONV_FLAT;
    conv_grad[idx] = concat_grad[b * PKR_ENC_CONCAT + c];
}

__global__ void pkr_concat_backward_scalar_kernel(
        precision_t* __restrict__ scalar_grad,
        const precision_t* __restrict__ concat_grad, int B) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= B * PKR_ENC_SCALAR_HIDDEN) {
        return;
    }
    int b = idx / PKR_ENC_SCALAR_HIDDEN;
    int c = idx % PKR_ENC_SCALAR_HIDDEN;
    scalar_grad[idx] = concat_grad[b * PKR_ENC_CONCAT + PKR_ENC_CONV_FLAT + c];
}

__global__ void pkr_concat_backward_party_kernel(
        precision_t* __restrict__ party_grad,
        const precision_t* __restrict__ concat_grad, int B) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= B * PKR_PARTY_HIDDEN) {
        return;
    }
    int b = idx / PKR_PARTY_HIDDEN;
    int c = idx % PKR_PARTY_HIDDEN;
    party_grad[idx] = concat_grad[
        b * PKR_ENC_CONCAT + PKR_ENC_CONV_FLAT + PKR_ENC_SCALAR_HIDDEN + c];
}

__global__ void pkr_party_embed_wgrad_kernel(
        precision_t* __restrict__ wgrad, const precision_t* __restrict__ grad_party_in,
        const int* __restrict__ species_idx, int B) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= PKR_PARTY_SPECIES_VOCAB * PKR_PARTY_EMBED_DIM) {
        return;
    }
    int v = idx / PKR_PARTY_EMBED_DIM;
    int e = idx % PKR_PARTY_EMBED_DIM;
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
    Prec conv1_w, conv2_w, scalar_w, party_embed_w, party_w, proj_w;
    int obs_size, hidden;
};

struct PokeredEncoderActivations {
    Prec conv1_out, conv1_grad, conv1_wgrad, col1, mm1;
    Prec conv2_out, conv2_grad, conv2_wgrad, col2, mm2;
    Prec scalar_in, scalar_out, scalar_grad, scalar_wgrad;
    Int party_species_idx;
    Prec party_in, party_out, party_grad, party_wgrad, party_embed_wgrad;
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

    pkr_gather_scalar_kernel<<<grid_size(B * PKR_ENC_SCALAR_IN), BLOCK_SIZE, 0, stream>>>(
        input.data, a->scalar_in.data, B, ew->obs_size);
    puf_mm(&a->scalar_in, &ew->scalar_w, &a->scalar_out, stream);
    pkr_relu_kernel<<<grid_size(B * PKR_ENC_SCALAR_HIDDEN), BLOCK_SIZE, 0, stream>>>(
        a->scalar_out.data, B * PKR_ENC_SCALAR_HIDDEN);

    pkr_party_species_kernel<<<grid_size(B * PARTY_SIZE), BLOCK_SIZE, 0, stream>>>(
        input.data, a->party_species_idx.data, B, ew->obs_size);
    pkr_party_gather_kernel<<<grid_size(B * PKR_PARTY_MLP_IN), BLOCK_SIZE, 0, stream>>>(
        input.data, ew->party_embed_w.data, a->party_species_idx.data, a->party_in.data,
        B, ew->obs_size);
    puf_mm(&a->party_in, &ew->party_w, &a->party_out, stream);
    pkr_relu_kernel<<<grid_size(B * PKR_PARTY_HIDDEN), BLOCK_SIZE, 0, stream>>>(
        a->party_out.data, B * PKR_PARTY_HIDDEN);

    pkr_concat_kernel<<<grid_size(B * PKR_ENC_CONCAT), BLOCK_SIZE, 0, stream>>>(
        a->concat.data, a->conv2_out.data, a->scalar_out.data, a->party_out.data, B);
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

    pkr_concat_backward_conv_kernel<<<grid_size(B * PKR_ENC_CONV_FLAT), BLOCK_SIZE, 0,
        stream>>>(a->conv2_grad.data, grad_concat.data, B);
    pkr_concat_backward_scalar_kernel<<<grid_size(B * PKR_ENC_SCALAR_HIDDEN), BLOCK_SIZE,
        0, stream>>>(a->scalar_grad.data, grad_concat.data, B);
    pkr_concat_backward_party_kernel<<<grid_size(B * PKR_PARTY_HIDDEN), BLOCK_SIZE,
        0, stream>>>(a->party_grad.data, grad_concat.data, B);

    pkr_relu_backward_kernel<<<grid_size(B * PKR_ENC_SCALAR_HIDDEN), BLOCK_SIZE, 0,
        stream>>>(a->scalar_grad.data, a->scalar_out.data, B * PKR_ENC_SCALAR_HIDDEN);
    Prec scalar_grad_t = {.data = a->scalar_grad.data, .shape = {B, PKR_ENC_SCALAR_HIDDEN}};
    puf_mm_tn(&scalar_grad_t, &a->scalar_in, &a->scalar_wgrad, stream);

    pkr_relu_backward_kernel<<<grid_size(B * PKR_PARTY_HIDDEN), BLOCK_SIZE, 0,
        stream>>>(a->party_grad.data, a->party_out.data, B * PKR_PARTY_HIDDEN);
    Prec party_grad_t = {.data = a->party_grad.data, .shape = {B, PKR_PARTY_HIDDEN}};
    puf_mm_tn(&party_grad_t, &a->party_in, &a->party_wgrad, stream);
    Prec grad_party_in = {.data = a->party_in.data, .shape = {B, PKR_PARTY_MLP_IN}};
    puf_mm_nn(&party_grad_t, &ew->party_w, &grad_party_in, stream);
    pkr_party_embed_wgrad_kernel<<<
        grid_size(PKR_PARTY_SPECIES_VOCAB * PKR_PARTY_EMBED_DIM), BLOCK_SIZE, 0,
        stream>>>(a->party_embed_wgrad.data, grad_party_in.data,
        a->party_species_idx.data, B);

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
    Prec sw = {.data = ew->scalar_w.data, .shape = {PKR_ENC_SCALAR_HIDDEN, PKR_ENC_SCALAR_IN}};
    Prec pw = {.data = ew->party_w.data, .shape = {PKR_PARTY_HIDDEN, PKR_PARTY_MLP_IN}};
    Prec proj = {.data = ew->proj_w.data, .shape = {ew->hidden, PKR_ENC_CONCAT}};
    puf_kaiming_init(&c1, sqrtf(2.0f), (*seed)++, stream);
    puf_kaiming_init(&c2, sqrtf(2.0f), (*seed)++, stream);
    puf_kaiming_init(&sw, sqrtf(2.0f), (*seed)++, stream);
    puf_kaiming_init(&pw, sqrtf(2.0f), (*seed)++, stream);
    puf_normal_init(&ew->party_embed_w, 0.02f, (*seed)++, stream);
    puf_kaiming_init(&proj, sqrtf(2.0f), (*seed)++, stream);
}

static void pokered_encoder_reg_params(void* w, Allocator* alloc) {
    PokeredEncoderWeights* ew = (PokeredEncoderWeights*)w;
    ew->conv1_w = {.shape = {PKR_ENC_C1_OC, PKR_ENC_C1_COL_W}};
    ew->conv2_w = {.shape = {PKR_ENC_C2_OC, PKR_ENC_C2_COL_W}};
    ew->scalar_w = {.shape = {PKR_ENC_SCALAR_HIDDEN, PKR_ENC_SCALAR_IN}};
    ew->party_embed_w = {.shape = {PKR_PARTY_SPECIES_VOCAB, PKR_PARTY_EMBED_DIM}};
    ew->party_w = {.shape = {PKR_PARTY_HIDDEN, PKR_PARTY_MLP_IN}};
    ew->proj_w = {.shape = {ew->hidden, PKR_ENC_CONCAT}};
    alloc_register(alloc, &ew->conv1_w);
    alloc_register(alloc, &ew->conv2_w);
    alloc_register(alloc, &ew->scalar_w);
    alloc_register(alloc, &ew->party_embed_w);
    alloc_register(alloc, &ew->party_w);
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
    a->scalar_in = {.shape = {B_TT, PKR_ENC_SCALAR_IN}};
    a->scalar_out = {.shape = {B_TT, PKR_ENC_SCALAR_HIDDEN}};
    a->scalar_grad = {.shape = {B_TT, PKR_ENC_SCALAR_HIDDEN}};
    a->scalar_wgrad = {.shape = {PKR_ENC_SCALAR_HIDDEN, PKR_ENC_SCALAR_IN}};
    a->party_species_idx = {.shape = {B_TT, PARTY_SIZE}};
    a->party_in = {.shape = {B_TT, PKR_PARTY_MLP_IN}};
    a->party_out = {.shape = {B_TT, PKR_PARTY_HIDDEN}};
    a->party_grad = {.shape = {B_TT, PKR_PARTY_HIDDEN}};
    a->party_wgrad = {.shape = {PKR_PARTY_HIDDEN, PKR_PARTY_MLP_IN}};
    a->party_embed_wgrad = {.shape = {PKR_PARTY_SPECIES_VOCAB, PKR_PARTY_EMBED_DIM}};
    a->concat = {.shape = {B_TT, PKR_ENC_CONCAT}};
    a->out = {.shape = {B_TT, ew->hidden}};
    a->proj_wgrad = {.shape = {ew->hidden, PKR_ENC_CONCAT}};
    alloc_register(acts, &a->conv1_out); alloc_register(acts, &a->conv1_grad);
    alloc_register(grads, &a->conv1_wgrad);
    alloc_register(acts, &a->col1); alloc_register(acts, &a->mm1);
    alloc_register(acts, &a->conv2_out); alloc_register(acts, &a->conv2_grad);
    alloc_register(grads, &a->conv2_wgrad);
    alloc_register(acts, &a->col2); alloc_register(acts, &a->mm2);
    alloc_register(acts, &a->scalar_in); alloc_register(acts, &a->scalar_out);
    alloc_register(acts, &a->scalar_grad); alloc_register(grads, &a->scalar_wgrad);
    alloc_register(acts, &a->party_species_idx);
    alloc_register(acts, &a->party_in); alloc_register(acts, &a->party_out);
    alloc_register(acts, &a->party_grad); alloc_register(grads, &a->party_wgrad);
    alloc_register(grads, &a->party_embed_wgrad);
    alloc_register(acts, &a->concat); alloc_register(acts, &a->out);
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
    a->scalar_in = {.shape = {B, PKR_ENC_SCALAR_IN}};
    a->scalar_out = {.shape = {B, PKR_ENC_SCALAR_HIDDEN}};
    a->party_species_idx = {.shape = {B, PARTY_SIZE}};
    a->party_in = {.shape = {B, PKR_PARTY_MLP_IN}};
    a->party_out = {.shape = {B, PKR_PARTY_HIDDEN}};
    a->concat = {.shape = {B, PKR_ENC_CONCAT}};
    a->out = {.shape = {B, ew->hidden}};
    alloc_register(alloc, &a->conv1_out);
    alloc_register(alloc, &a->col1); alloc_register(alloc, &a->mm1);
    alloc_register(alloc, &a->conv2_out);
    alloc_register(alloc, &a->col2); alloc_register(alloc, &a->mm2);
    alloc_register(alloc, &a->scalar_in); alloc_register(alloc, &a->scalar_out);
    alloc_register(alloc, &a->party_species_idx);
    alloc_register(alloc, &a->party_in); alloc_register(alloc, &a->party_out);
    alloc_register(alloc, &a->concat); alloc_register(alloc, &a->out);
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
