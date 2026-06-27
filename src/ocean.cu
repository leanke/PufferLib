// NMMO3 CUDA encoder: multihot, cuDNN conv, embedding, concat, projection
// Included by pufferlib.cu — requires precision_t, PrecisionTensor, Allocator, puf_mm, etc.

#include "cudnn_conv2d.cu"

// ---- NMMO3 constants ----

static constexpr int N3_MAP_H = 11, N3_MAP_W = 15, N3_NFEAT = 10;
static constexpr int N3_MULTIHOT = 59;
static constexpr int N3_MAP_SIZE = N3_MAP_H * N3_MAP_W * N3_NFEAT;
static constexpr int N3_PLAYER = 47, N3_REWARD = 10;
static constexpr int N3_EMBED_DIM = 32, N3_EMBED_VOCAB = 128;
static constexpr int N3_PLAYER_EMBED = N3_PLAYER * N3_EMBED_DIM;
static constexpr int N3_C1_IC = 59, N3_C1_OC = 128, N3_C1_K = 5, N3_C1_S = 3;
static constexpr int N3_C1_OH = 3, N3_C1_OW = 4;
static constexpr int N3_C2_IC = 128, N3_C2_OC = 128, N3_C2_K = 3, N3_C2_S = 1;
static constexpr int N3_C2_OH = 1, N3_C2_OW = 2;
static constexpr int N3_CONV_FLAT = N3_C2_OC * N3_C2_OH * N3_C2_OW;
static constexpr int N3_CONCAT = N3_CONV_FLAT + N3_PLAYER_EMBED + N3_PLAYER + N3_REWARD;

__constant__ int N3_OFFSETS[10] = {0, 4, 8, 25, 30, 33, 38, 43, 48, 55};

static cudnnDataType_t n3_cudnn_dtype() {
    return (PRECISION_SIZE == 2) ? CUDNN_DATA_BFLOAT16 : CUDNN_DATA_FLOAT;
}

// ---- NMMO3 kernels ----

__global__ void n3_multihot_kernel(
    precision_t* __restrict__ out, const precision_t* __restrict__ obs, int B, int obs_size) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= B * N3_MAP_H * N3_MAP_W) return;
    int b = idx / (N3_MAP_H * N3_MAP_W), rem = idx % (N3_MAP_H * N3_MAP_W);
    int h = rem / N3_MAP_W, w = rem % N3_MAP_W;
    const precision_t* src = obs + b * obs_size + (h * N3_MAP_W + w) * N3_NFEAT;
    precision_t* dst = out + b * N3_MULTIHOT * N3_MAP_H * N3_MAP_W;
    for (int f = 0; f < N3_NFEAT; f++)
        dst[(N3_OFFSETS[f] + (int)to_float(src[f])) * N3_MAP_H * N3_MAP_W + h * N3_MAP_W + w] = from_float(1.0f);
}

__global__ void n3_embedding_kernel(
    precision_t* __restrict__ out, const precision_t* __restrict__ obs,
    const precision_t* __restrict__ embed_w, int B, int obs_size) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= B * N3_PLAYER) return;
    int b = idx / N3_PLAYER, f = idx % N3_PLAYER;
    int val = (int)to_float(obs[b * obs_size + N3_MAP_SIZE + f]);
    const precision_t* src = embed_w + val * N3_EMBED_DIM;
    precision_t* dst = out + b * N3_PLAYER_EMBED + f * N3_EMBED_DIM;
    for (int d = 0; d < N3_EMBED_DIM; d++) dst[d] = src[d];
}

__global__ void n3_concat_kernel(
    precision_t* __restrict__ out, const precision_t* __restrict__ conv_flat,
    const precision_t* __restrict__ embed, const precision_t* __restrict__ obs,
    int B, int obs_size) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= B * N3_CONCAT) return;
    int b = idx / N3_CONCAT, c = idx % N3_CONCAT;
    precision_t val;
    if (c < N3_CONV_FLAT) {
        int oc = c / (N3_C2_OH * N3_C2_OW), r = c % (N3_C2_OH * N3_C2_OW);
        int oh = r / N3_C2_OW, ow = r % N3_C2_OW;
        val = conv_flat[b * N3_CONV_FLAT + oc * N3_C2_OH * N3_C2_OW + oh * N3_C2_OW + ow];
    } else if (c < N3_CONV_FLAT + N3_PLAYER_EMBED)
        val = embed[b * N3_PLAYER_EMBED + (c - N3_CONV_FLAT)];
    else if (c < N3_CONV_FLAT + N3_PLAYER_EMBED + N3_PLAYER)
        val = obs[b * obs_size + N3_MAP_SIZE + (c - N3_CONV_FLAT - N3_PLAYER_EMBED)];
    else
        val = obs[b * obs_size + obs_size - N3_REWARD + (c - N3_CONV_FLAT - N3_PLAYER_EMBED - N3_PLAYER)];
    out[idx] = val;
}

__global__ void n3_bias_relu_kernel(
    precision_t* __restrict__ data, const precision_t* __restrict__ bias, int total, int dim) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= total) return;
    data[idx] = from_float(fmaxf(0.0f, to_float(data[idx]) + to_float(bias[idx % dim])));
}

__global__ void n3_relu_backward_kernel(
    precision_t* __restrict__ grad, const precision_t* __restrict__ out, int total) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= total) return;
    if (to_float(out[idx]) <= 0.0f) grad[idx] = from_float(0.0f);
}


__global__ void bias_grad_kernel(
    precision_t* __restrict__ bgrad, const precision_t* __restrict__ grad, int N, int dim) {
    int d = blockIdx.x;
    if (d >= dim) return;
    float sum = 0.0f;
    for (int i = threadIdx.x; i < N; i += blockDim.x)
        sum += to_float(grad[i * dim + d]);
    for (int offset = 16; offset > 0; offset >>= 1)
        sum += __shfl_down_sync(0xffffffff, sum, offset);
    __shared__ float sdata[32];
    int lane = threadIdx.x % 32, warp = threadIdx.x / 32;
    if (lane == 0) sdata[warp] = sum;
    __syncthreads();
    if (warp == 0) {
        sum = (lane < (blockDim.x + 31) / 32) ? sdata[lane] : 0.0f;
        for (int offset = 16; offset > 0; offset >>= 1)
            sum += __shfl_down_sync(0xffffffff, sum, offset);
        if (lane == 0) bgrad[d] = from_float(sum);
    }
}

// NCHW bias grad: sum over (B, OH, OW) for each OC channel
__global__ void n3_conv_bias_grad_nchw(
    precision_t* __restrict__ bgrad, const precision_t* __restrict__ grad,
    int B, int OC, int spatial) {
    int oc = blockIdx.x;
    if (oc >= OC) return;
    float sum = 0.0f;
    int total = B * spatial;
    for (int i = threadIdx.x; i < total; i += blockDim.x) {
        int b = i / spatial, s = i % spatial;
        sum += to_float(grad[b * OC * spatial + oc * spatial + s]);
    }
    for (int offset = 16; offset > 0; offset >>= 1)
        sum += __shfl_down_sync(0xffffffff, sum, offset);
    __shared__ float sdata[32];
    int lane = threadIdx.x % 32, warp = threadIdx.x / 32;
    if (lane == 0) sdata[warp] = sum;
    __syncthreads();
    if (warp == 0) {
        sum = (lane < (blockDim.x + 31) / 32) ? sdata[lane] : 0.0f;
        for (int offset = 16; offset > 0; offset >>= 1)
            sum += __shfl_down_sync(0xffffffff, sum, offset);
        if (lane == 0) bgrad[oc] = from_float(sum);
    }
}

__global__ void n3_concat_backward_conv_kernel(
    precision_t* __restrict__ conv_grad, const precision_t* __restrict__ concat_grad, int B) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= B * N3_CONV_FLAT) return;
    int b = idx / N3_CONV_FLAT, c = idx % N3_CONV_FLAT;
    conv_grad[b * N3_CONV_FLAT + c] = concat_grad[b * N3_CONCAT + c];
}

// Embedding backward: scatter-add grad from concat_grad's player_embed region
// into embed_wgrad (float accumulation buffer).
// Each (b, f) looked up row obs[b, MAP_SIZE+f] from the table.
__global__ void n3_embedding_backward_kernel(
    float* __restrict__ embed_wgrad_f,
    const precision_t* __restrict__ concat_grad,
    const precision_t* __restrict__ obs,
    int B, int obs_size) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= B * N3_PLAYER * N3_EMBED_DIM) return;
    int b = idx / (N3_PLAYER * N3_EMBED_DIM);
    int rem = idx % (N3_PLAYER * N3_EMBED_DIM);
    int f = rem / N3_EMBED_DIM;
    int d = rem % N3_EMBED_DIM;
    int val = (int)to_float(obs[b * obs_size + N3_MAP_SIZE + f]);
    float g = to_float(concat_grad[b * N3_CONCAT + N3_CONV_FLAT + f * N3_EMBED_DIM + d]);
    atomicAdd(&embed_wgrad_f[val * N3_EMBED_DIM + d], g);
}

// Cast float buffer to precision_t
__global__ void n3_float_to_precision_kernel(
    precision_t* __restrict__ dst, const float* __restrict__ src, int n) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx < n) dst[idx] = from_float(src[idx]);
}

// ---- atomicAdd for precision_t ----
#ifdef PRECISION_FLOAT
__device__ __forceinline__ void atomicAdd_precision(precision_t* addr, precision_t val) {
    atomicAdd(addr, val);
}
#else
__device__ __forceinline__ void atomicAdd_precision(precision_t* addr, precision_t val) {
    // bf16 atomicAdd via CAS on enclosing 32-bit word
    unsigned int* addr_u32 = (unsigned int*)((size_t)addr & ~2ULL);
    bool is_high = ((size_t)addr & 2) != 0;
    unsigned int old_u32 = *addr_u32, assumed;
    do {
        assumed = old_u32;
        __nv_bfloat16* pair = (__nv_bfloat16*)&old_u32;
        float sum = __bfloat162float(pair[is_high]) + __bfloat162float(val);
        unsigned int new_u32 = assumed;
        ((__nv_bfloat16*)&new_u32)[is_high] = __float2bfloat16(sum);
        old_u32 = atomicCAS(addr_u32, assumed, new_u32);
    } while (old_u32 != assumed);
}
#endif

// ---- NCHW bias kernels for im2col conv path ----

__global__ void conv_bias_kernel(precision_t* __restrict__ data,
        const precision_t* __restrict__ bias, int B, int OC, int spatial) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    int total = B * OC * spatial;
    if (idx >= total) return;
    int oc = (idx / spatial) % OC;
    data[idx] = from_float(to_float(data[idx]) + to_float(bias[oc]));
}

__global__ void conv_bias_relu_kernel(precision_t* __restrict__ data,
        const precision_t* __restrict__ bias, int B, int OC, int spatial) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    int total = B * OC * spatial;
    if (idx >= total) return;
    int oc = (idx / spatial) % OC;
    data[idx] = from_float(fmaxf(0.0f, to_float(data[idx]) + to_float(bias[oc])));
}

// ---- im2col + cuBLAS conv (no cuDNN) ----
// NCHW layout throughout. Weight stored as (OC, IC*K*K).
// im2col produces (B*OH*OW, IC*K*K), matmul with W^T gives (B*OH*OW, OC),
// then reshape to NCHW (B, OC, OH, OW).

__global__ void im2col_kernel(
    const precision_t* __restrict__ input, precision_t* __restrict__ col,
    int B, int IC, int IH, int IW, int K, int S, int OH, int OW
) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    int total = B * OH * OW * IC * K * K;
    if (idx >= total) return;
    int col_w = IC * K * K;
    int row = idx / col_w;
    int c = idx % col_w;
    int b = row / (OH * OW);
    int rem = row % (OH * OW);
    int oh = rem / OW, ow = rem % OW;
    int ic = c / (K * K), kk = c % (K * K);
    int kh = kk / K, kw = kk % K;
    int ih = oh * S + kh, iw = ow * S + kw;
    col[idx] = input[b * IC * IH * IW + ic * IH * IW + ih * IW + iw];
}

// Backward: col2im — input-centric gather to avoid atomics.
// Each thread owns one (b, ic, ih, iw) element and sums contributions from all
// (oh, ow, kh, kw) patches that map to it.
__global__ void col2im_kernel(
    const precision_t* __restrict__ col, precision_t* __restrict__ grad_input,
    int B, int IC, int IH, int IW, int K, int S, int OH, int OW
) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    int total = B * IC * IH * IW;
    if (idx >= total) return;
    int iw = idx % IW;
    int ih = (idx / IW) % IH;
    int ic = (idx / (IW * IH)) % IC;
    int b  = idx / (IW * IH * IC);
    float sum = 0.0f;
    for (int kh = 0; kh < K; kh++) {
        int ih_off = ih - kh;
        if (ih_off < 0 || ih_off % S != 0) continue;
        int oh = ih_off / S;
        if (oh >= OH) continue;
        for (int kw = 0; kw < K; kw++) {
            int iw_off = iw - kw;
            if (iw_off < 0 || iw_off % S != 0) continue;
            int ow = iw_off / S;
            if (ow >= OW) continue;
            int col_idx = (b * OH * OW + oh * OW + ow) * (IC * K * K) + ic * K * K + kh * K + kw;
            sum += to_float(col[col_idx]);
        }
    }
    grad_input[idx] = from_float(sum);
}

// Transpose (B, OC, OH, OW) -> (B*OH*OW, OC)  [NCHW to row-major spatial-first]
__global__ void nchw_to_rows_kernel(
    const precision_t* __restrict__ src, precision_t* __restrict__ dst,
    int B, int OC, int spatial
) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    int total = B * OC * spatial;
    if (idx >= total) return;
    int b = idx / (OC * spatial);
    int oc = (idx / spatial) % OC;
    int s = idx % spatial;
    dst[(b * spatial + s) * OC + oc] = src[idx];
}

// Transpose (B*OH*OW, OC) -> (B, OC, OH, OW)  [row-major spatial-first to NCHW]
__global__ void rows_to_nchw_kernel(
    const precision_t* __restrict__ src, precision_t* __restrict__ dst,
    int B, int OC, int spatial
) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    int total = B * OC * spatial;
    if (idx >= total) return;
    int b = idx / (OC * spatial);
    int oc = (idx / spatial) % OC;
    int s = idx % spatial;
    dst[idx] = src[(b * spatial + s) * OC + oc];
}

// Forward: im2col conv + bias + optional relu. All NCHW.
// col_buf: pre-allocated (max_B * OH * OW, IC * K * K)
// mm_buf:  pre-allocated (max_B * OH * OW, OC)  — row-major (spatial-first)
static void gemm_conv_forward(
    PrecisionTensor* weight, PrecisionTensor* bias,
    precision_t* input, precision_t* output,
    precision_t* col_buf, precision_t* mm_buf,
    int B, int IC, int IH, int IW, int OC, int K, int S, int OH, int OW,
    bool relu, cudaStream_t stream
) {
    int col_rows = B * OH * OW;
    int col_cols = IC * K * K;
    int total_col = col_rows * col_cols;
    int total_out = B * OC * OH * OW;

    // im2col: input NCHW -> col (B*OH*OW, IC*K*K)
    im2col_kernel<<<grid_size(total_col), BLOCK_SIZE, 0, stream>>>(
        input, col_buf, B, IC, IH, IW, K, S, OH, OW);

    // matmul: col (B*OH*OW, IC*K*K) @ W^T (IC*K*K, OC) = mm_buf (B*OH*OW, OC)
    PrecisionTensor col_t = {.data = col_buf, .shape = {col_rows, col_cols}};
    PrecisionTensor mm_t  = {.data = mm_buf,  .shape = {col_rows, OC}};
    puf_mm(&col_t, weight, &mm_t, stream);

    // transpose (B*OH*OW, OC) -> (B, OC, OH, OW) NCHW + bias + relu
    int spatial = OH * OW;
    rows_to_nchw_kernel<<<grid_size(total_out), BLOCK_SIZE, 0, stream>>>(
        mm_buf, output, B, OC, spatial);
    if (relu) {
        conv_bias_relu_kernel<<<grid_size(total_out), BLOCK_SIZE, 0, stream>>>(
            output, bias->data, B, OC, spatial);
    } else {
        conv_bias_kernel<<<grid_size(total_out), BLOCK_SIZE, 0, stream>>>(
            output, bias->data, B, OC, spatial);
    }
}

// Backward: weight grad + optional input grad via im2col/col2im + cuBLAS.
// grad_output is NCHW (B, OC, OH, OW). saved_input is NCHW.
// Caller handles relu backward and bias grad (same as cuDNN path).
static void gemm_conv_backward(
    PrecisionTensor* weight,
    precision_t* saved_input, precision_t* grad_output,
    precision_t* wgrad, precision_t* input_grad,
    precision_t* col_buf, precision_t* mm_buf,
    int B, int IC, int IH, int IW, int OC, int K, int S, int OH, int OW,
    cudaStream_t stream
) {
    int col_rows = B * OH * OW;
    int col_cols = IC * K * K;
    int total_col = col_rows * col_cols;
    int total_out = B * OC * OH * OW;
    int spatial = OH * OW;

    // Transpose grad_output NCHW -> (B*OH*OW, OC)
    nchw_to_rows_kernel<<<grid_size(total_out), BLOCK_SIZE, 0, stream>>>(
        grad_output, mm_buf, B, OC, spatial);

    // im2col of saved_input
    im2col_kernel<<<grid_size(total_col), BLOCK_SIZE, 0, stream>>>(
        saved_input, col_buf, B, IC, IH, IW, K, S, OH, OW);

    // Weight grad: mm_buf^T (OC, B*OH*OW) @ col_buf (B*OH*OW, IC*K*K) = wgrad (OC, IC*K*K)
    PrecisionTensor mm_t  = {.data = mm_buf,  .shape = {col_rows, OC}};
    PrecisionTensor col_t = {.data = col_buf, .shape = {col_rows, col_cols}};
    PrecisionTensor wg_t  = {.data = wgrad,   .shape = {OC, col_cols}};
    puf_mm_tn(&mm_t, &col_t, &wg_t, stream);

    // Input grad (optional): mm_buf (B*OH*OW, OC) @ weight (OC, IC*K*K) = col_grad (B*OH*OW, IC*K*K)
    if (input_grad) {
        puf_mm_nn(&mm_t, weight, &col_t, stream);  // reuse col_buf as col_grad
        col2im_kernel<<<grid_size(B * IC * IH * IW), BLOCK_SIZE, 0, stream>>>(
            col_buf, input_grad, B, IC, IH, IW, K, S, OH, OW);
    }
}

// ---- NMMO3 encoder structs ----

struct NMMO3EncoderWeights {
    ConvWeights conv1, conv2;
    PrecisionTensor embed_w, proj_w, proj_b;
    int obs_size, hidden;
};

struct NMMO3EncoderActivations {
    ConvActivations conv1, conv2;
    PrecisionTensor col1, mm1, col2, mm2;  // im2col + matmul scratch buffers
    PrecisionTensor multihot, embed_out, concat, out, saved_obs;
    PrecisionTensor embed_wgrad, proj_wgrad, proj_bgrad;
    FloatTensor embed_wgrad_f;  // float accumulation buffer for scatter-add
};

static NMMO3EncoderWeights* nmmo3_encoder_create(int obs_size, int hidden) {
    NMMO3EncoderWeights* ew = (NMMO3EncoderWeights*)calloc(1, sizeof(NMMO3EncoderWeights));
    ew->obs_size = obs_size; ew->hidden = hidden;
    conv_init(&ew->conv1, N3_C1_IC, N3_C1_OC, N3_C1_K, N3_C1_S, N3_MAP_H, N3_MAP_W, true);
    conv_init(&ew->conv2, N3_C2_IC, N3_C2_OC, N3_C2_K, N3_C2_S, N3_C1_OH, N3_C1_OW, false);
    return ew;
}

// ---- NMMO3 encoder interface ----

static PrecisionTensor nmmo3_encoder_forward(void* w, void* activations, PrecisionTensor input, cudaStream_t stream) {
    NMMO3EncoderWeights* ew = (NMMO3EncoderWeights*)w;
    NMMO3EncoderActivations* a = (NMMO3EncoderActivations*)activations;
    int B = input.shape[0];

    if (a->saved_obs.data) puf_copy(&a->saved_obs, &input, stream);

    cudaMemsetAsync(a->multihot.data, 0, (int64_t)B * N3_MULTIHOT * N3_MAP_H * N3_MAP_W * sizeof(precision_t), stream);
    n3_multihot_kernel<<<grid_size(B * N3_MAP_H * N3_MAP_W), BLOCK_SIZE, 0, stream>>>(
        a->multihot.data, input.data, B, ew->obs_size);

    gemm_conv_forward(&ew->conv1.w, &ew->conv1.b, a->multihot.data, a->conv1.out.data,
        a->col1.data, a->mm1.data, B, N3_C1_IC, N3_MAP_H, N3_MAP_W,
        N3_C1_OC, N3_C1_K, N3_C1_S, N3_C1_OH, N3_C1_OW, true, stream);
    if (a->conv1.saved_input.data)
        cudaMemcpyAsync(a->conv1.saved_input.data, a->multihot.data,
            (int64_t)B * N3_C1_IC * N3_MAP_H * N3_MAP_W * sizeof(precision_t), cudaMemcpyDeviceToDevice, stream);
    gemm_conv_forward(&ew->conv2.w, &ew->conv2.b, a->conv1.out.data, a->conv2.out.data,
        a->col2.data, a->mm2.data, B, N3_C2_IC, N3_C1_OH, N3_C1_OW,
        N3_C2_OC, N3_C2_K, N3_C2_S, N3_C2_OH, N3_C2_OW, false, stream);
    if (a->conv2.saved_input.data)
        cudaMemcpyAsync(a->conv2.saved_input.data, a->conv1.out.data,
            (int64_t)B * N3_C2_IC * N3_C1_OH * N3_C1_OW * sizeof(precision_t), cudaMemcpyDeviceToDevice, stream);

    n3_embedding_kernel<<<grid_size(B * N3_PLAYER), BLOCK_SIZE, 0, stream>>>(
        a->embed_out.data, input.data, ew->embed_w.data, B, ew->obs_size);
    n3_concat_kernel<<<grid_size(B * N3_CONCAT), BLOCK_SIZE, 0, stream>>>(
        a->concat.data, a->conv2.out.data, a->embed_out.data, input.data, B, ew->obs_size);

    puf_mm(&a->concat, &ew->proj_w, &a->out, stream);
    n3_bias_relu_kernel<<<grid_size(B * ew->hidden), BLOCK_SIZE, 0, stream>>>(
        a->out.data, ew->proj_b.data, B * ew->hidden, ew->hidden);
    return a->out;
}

static void nmmo3_encoder_backward(void* w, void* activations, PrecisionTensor grad, cudaStream_t stream) {
    NMMO3EncoderWeights* ew = (NMMO3EncoderWeights*)w;
    NMMO3EncoderActivations* a = (NMMO3EncoderActivations*)activations;
    int B = grad.shape[0], H = ew->hidden;

    n3_relu_backward_kernel<<<grid_size(B * H), BLOCK_SIZE, 0, stream>>>(
        grad.data, a->out.data, B * H);
    bias_grad_kernel<<<H, 256, 0, stream>>>(
        a->proj_bgrad.data, grad.data, B, H);
    puf_mm_tn(&grad, &a->concat, &a->proj_wgrad, stream);

    PrecisionTensor grad_concat = {.data = a->concat.data, .shape = {B, N3_CONCAT}};
    puf_mm_nn(&grad, &ew->proj_w, &grad_concat, stream);

    n3_concat_backward_conv_kernel<<<grid_size(B * N3_CONV_FLAT), BLOCK_SIZE, 0, stream>>>(
        a->conv2.grad.data, grad_concat.data, B);

    n3_conv_bias_grad_nchw<<<ew->conv2.OC, 256, 0, stream>>>(
        a->conv2.bgrad.data, a->conv2.grad.data,
        B, ew->conv2.OC, ew->conv2.OH * ew->conv2.OW);
    gemm_conv_backward(&ew->conv2.w, a->conv2.saved_input.data, a->conv2.grad.data,
        a->conv2.wgrad.data, a->conv1.grad.data,
        a->col2.data, a->mm2.data, B, N3_C2_IC, N3_C1_OH, N3_C1_OW,
        N3_C2_OC, N3_C2_K, N3_C2_S, N3_C2_OH, N3_C2_OW, stream);

    n3_relu_backward_kernel<<<grid_size(B * ew->conv1.OC * ew->conv1.OH * ew->conv1.OW), BLOCK_SIZE, 0, stream>>>(
        a->conv1.grad.data, a->conv1.out.data,
        B * ew->conv1.OC * ew->conv1.OH * ew->conv1.OW);
    n3_conv_bias_grad_nchw<<<ew->conv1.OC, 256, 0, stream>>>(
        a->conv1.bgrad.data, a->conv1.grad.data,
        B, ew->conv1.OC, ew->conv1.OH * ew->conv1.OW);
    gemm_conv_backward(&ew->conv1.w, a->conv1.saved_input.data, a->conv1.grad.data,
        a->conv1.wgrad.data, NULL,
        a->col1.data, a->mm1.data, B, N3_C1_IC, N3_MAP_H, N3_MAP_W,
        N3_C1_OC, N3_C1_K, N3_C1_S, N3_C1_OH, N3_C1_OW, stream);

    // Embedding backward: scatter-add from concat gradient into float buffer, then cast
    int embed_n = N3_EMBED_VOCAB * N3_EMBED_DIM;
    cudaMemsetAsync(a->embed_wgrad_f.data, 0, embed_n * sizeof(float), stream);
    n3_embedding_backward_kernel<<<grid_size(B * N3_PLAYER * N3_EMBED_DIM), BLOCK_SIZE, 0, stream>>>(
        a->embed_wgrad_f.data, grad_concat.data, a->saved_obs.data, B, ew->obs_size);
    n3_float_to_precision_kernel<<<grid_size(embed_n), BLOCK_SIZE, 0, stream>>>(
        a->embed_wgrad.data, a->embed_wgrad_f.data, embed_n);
}

static void nmmo3_encoder_init_weights(void* w, uint64_t* seed, cudaStream_t stream) {
    NMMO3EncoderWeights* ew = (NMMO3EncoderWeights*)w;
    conv_init_weights(&ew->conv1, seed, stream);
    conv_init_weights(&ew->conv2, seed, stream);
    auto init2d = [&](PrecisionTensor& t, int rows, int cols, float gain) {
        PrecisionTensor wt = {.data = t.data, .shape = {rows, cols}};
        puf_kaiming_init(&wt, gain, (*seed)++, stream);
    };
    puf_normal_init(&ew->embed_w, 1.0f, (*seed)++, stream);
    init2d(ew->proj_w, ew->hidden, N3_CONCAT, 1.0f);
    cudaMemsetAsync(ew->proj_b.data, 0, numel(ew->proj_b.shape) * sizeof(precision_t), stream);
}

static void nmmo3_encoder_reg_params(void* w, Allocator* alloc) {
    NMMO3EncoderWeights* ew = (NMMO3EncoderWeights*)w;
    conv_reg_params(&ew->conv1, alloc);
    conv_reg_params(&ew->conv2, alloc);
    ew->embed_w = {.shape = {N3_EMBED_VOCAB, N3_EMBED_DIM}};
    ew->proj_w  = {.shape = {ew->hidden, N3_CONCAT}};
    ew->proj_b  = {.shape = {ew->hidden}};
    alloc_register(alloc,&ew->embed_w);
    alloc_register(alloc,&ew->proj_w);  alloc_register(alloc,&ew->proj_b);
}

static void nmmo3_encoder_reg_train(void* w, void* activations, Allocator* acts, Allocator* grads, int B_TT) {
    NMMO3EncoderWeights* ew = (NMMO3EncoderWeights*)w;
    NMMO3EncoderActivations* a = (NMMO3EncoderActivations*)activations;
    *a = {};
    a->multihot = {.shape = {B_TT, N3_MULTIHOT * N3_MAP_H * N3_MAP_W}};
    alloc_register(acts,&a->multihot);
    // Conv1 buffers
    a->conv1.out         = {.shape = {B_TT * N3_C1_OC * N3_C1_OH * N3_C1_OW}};
    a->conv1.grad        = {.shape = {B_TT * N3_C1_OC * N3_C1_OH * N3_C1_OW}};
    a->conv1.saved_input = {.shape = {B_TT * N3_C1_IC * N3_MAP_H * N3_MAP_W}};
    a->conv1.wgrad       = {.shape = {N3_C1_OC, N3_C1_IC * N3_C1_K * N3_C1_K}};
    a->conv1.bgrad       = {.shape = {N3_C1_OC}};
    alloc_register(acts,&a->conv1.out); alloc_register(acts,&a->conv1.grad); alloc_register(acts,&a->conv1.saved_input);
    alloc_register(grads,&a->conv1.wgrad); alloc_register(grads,&a->conv1.bgrad);
    a->col1 = {.shape = {B_TT * N3_C1_OH * N3_C1_OW, N3_C1_IC * N3_C1_K * N3_C1_K}};
    a->mm1  = {.shape = {B_TT * N3_C1_OH * N3_C1_OW, N3_C1_OC}};
    alloc_register(acts,&a->col1); alloc_register(acts,&a->mm1);
    // Conv2 buffers
    a->conv2.out         = {.shape = {B_TT * N3_C2_OC * N3_C2_OH * N3_C2_OW}};
    a->conv2.grad        = {.shape = {B_TT * N3_C2_OC * N3_C2_OH * N3_C2_OW}};
    a->conv2.saved_input = {.shape = {B_TT * N3_C2_IC * N3_C1_OH * N3_C1_OW}};
    a->conv2.wgrad       = {.shape = {N3_C2_OC, N3_C2_IC * N3_C2_K * N3_C2_K}};
    a->conv2.bgrad       = {.shape = {N3_C2_OC}};
    alloc_register(acts,&a->conv2.out); alloc_register(acts,&a->conv2.grad); alloc_register(acts,&a->conv2.saved_input);
    alloc_register(grads,&a->conv2.wgrad); alloc_register(grads,&a->conv2.bgrad);
    a->col2 = {.shape = {B_TT * N3_C2_OH * N3_C2_OW, N3_C2_IC * N3_C2_K * N3_C2_K}};
    a->mm2  = {.shape = {B_TT * N3_C2_OH * N3_C2_OW, N3_C2_OC}};
    alloc_register(acts,&a->col2); alloc_register(acts,&a->mm2);
    a->embed_out = {.shape = {B_TT, N3_PLAYER_EMBED}};
    a->concat    = {.shape = {B_TT, N3_CONCAT}};
    a->out       = {.shape = {B_TT, ew->hidden}};
    a->saved_obs = {.shape = {B_TT, ew->obs_size}};
    alloc_register(acts,&a->embed_out); alloc_register(acts,&a->concat);
    alloc_register(acts,&a->out);       alloc_register(acts,&a->saved_obs);
    a->embed_wgrad = {.shape = {N3_EMBED_VOCAB, N3_EMBED_DIM}};
    a->embed_wgrad_f = {.shape = {N3_EMBED_VOCAB, N3_EMBED_DIM}};
    a->proj_wgrad  = {.shape = {ew->hidden, N3_CONCAT}};
    a->proj_bgrad  = {.shape = {ew->hidden}};
    alloc_register(grads,&a->embed_wgrad);
    alloc_register(acts,&a->embed_wgrad_f);
    alloc_register(grads,&a->proj_wgrad);  alloc_register(grads,&a->proj_bgrad);
}

static void nmmo3_encoder_reg_rollout(void* w, void* activations, Allocator* alloc, int B) {
    NMMO3EncoderWeights* ew = (NMMO3EncoderWeights*)w;
    NMMO3EncoderActivations* a = (NMMO3EncoderActivations*)activations;
    a->multihot = {.shape = {B, N3_MULTIHOT * N3_MAP_H * N3_MAP_W}};
    alloc_register(alloc,&a->multihot);
    a->conv1.out = {.shape = {B * N3_C1_OC * N3_C1_OH * N3_C1_OW}};
    alloc_register(alloc,&a->conv1.out);
    a->col1 = {.shape = {B * N3_C1_OH * N3_C1_OW, N3_C1_IC * N3_C1_K * N3_C1_K}};
    a->mm1  = {.shape = {B * N3_C1_OH * N3_C1_OW, N3_C1_OC}};
    alloc_register(alloc,&a->col1); alloc_register(alloc,&a->mm1);
    a->conv2.out = {.shape = {B * N3_C2_OC * N3_C2_OH * N3_C2_OW}};
    alloc_register(alloc,&a->conv2.out);
    a->col2 = {.shape = {B * N3_C2_OH * N3_C2_OW, N3_C2_IC * N3_C2_K * N3_C2_K}};
    a->mm2  = {.shape = {B * N3_C2_OH * N3_C2_OW, N3_C2_OC}};
    alloc_register(alloc,&a->col2); alloc_register(alloc,&a->mm2);
    a->embed_out = {.shape = {B, N3_PLAYER_EMBED}};
    a->concat    = {.shape = {B, N3_CONCAT}};
    a->out       = {.shape = {B, ew->hidden}};
    alloc_register(alloc,&a->embed_out); alloc_register(alloc,&a->concat); alloc_register(alloc,&a->out);
}

static void* nmmo3_encoder_create_weights(void* self) {
    Encoder* e = (Encoder*)self;
    return nmmo3_encoder_create(e->in_dim, e->out_dim);
}
static void nmmo3_encoder_free_weights(void* weights) { free(weights); }
static void nmmo3_encoder_free_activations(void* activations) { free(activations); }

// ===================================================================
// Terraria CUDA encoder
// ===================================================================

// ---- Constants (match observation.h layout) ----
static constexpr int T_IH = 9, T_IW = 21, T_IC = 2;
// Conv1: pad=1, stride=1, k=3 → same spatial (9, 21)
static constexpr int T_C1_OC = 16, T_C1_K = 3, T_C1_P = 1;
static constexpr int T_C1_OH = 9, T_C1_OW = 21;
// MaxPool2d(2) → floor(9/2)=4, floor(21/2)=10
static constexpr int T_MP_OH = 4, T_MP_OW = 10;
// Conv2: pad=1, stride=1, k=3 → same spatial (4, 10)
static constexpr int T_C2_IC = 16, T_C2_OC = 32, T_C2_K = 3, T_C2_P = 1;
static constexpr int T_C2_OH = 4, T_C2_OW = 10;
static constexpr int T_FLAT = T_C2_OC * T_C2_OH * T_C2_OW;  // 1280
static constexpr int T_TILE_OUT = 64;
static constexpr int T_SCALAR_IN = 98;   // player(6)+world(2)+inv(64)+equip(6)+crafting(20)
static constexpr int T_SCALAR_OUT = 32;
static constexpr int T_ENEMY_IN = 4, T_ENEMY_OUT = 16, T_NEMY = 8;
static constexpr int T_CONCAT_IN = T_TILE_OUT + T_SCALAR_OUT + T_ENEMY_OUT;  // 112

struct TerrariaEncoderWeights {
    PrecisionTensor conv1_w, conv1_b;     // (16, 2*3*3=18), (16,)
    PrecisionTensor conv2_w, conv2_b;     // (32, 16*3*3=144), (32,)
    PrecisionTensor tile_w, tile_b;       // (64, 1280), (64,)
    PrecisionTensor scalar_w, scalar_b;   // (32, 98), (32,)
    PrecisionTensor enemy_w, enemy_b;     // (16, 4), (16,)
    PrecisionTensor out_w, out_b;         // (hidden, 112), (hidden,)
    int hidden;
};

struct TerrariaEncoderActivations {
    PrecisionTensor tiles_nchw;     // (B, 2, 9, 21) permuted tile input; saved for wgrad
    PrecisionTensor conv1_out;      // (B, 16, 9, 21) post-relu
    PrecisionTensor mp_out;         // (B, 16, 4, 10)
    PrecisionTensor mp_argmax;      // (B, 16, 4, 10) stored as precision_t
    PrecisionTensor conv2_out;      // (B, 32, 4, 10) post-relu; also serves as tile_flat (B,1280)
    PrecisionTensor tile_out;       // (B, 64) post-relu
    PrecisionTensor col1, mm1;      // im2col scratch for conv1
    PrecisionTensor col2, mm2;      // im2col scratch for conv2
    PrecisionTensor scalar_buf;     // (B, 98)
    PrecisionTensor scalar_out;     // (B, 32) post-relu
    PrecisionTensor enemy_flat;     // (B*8, 4)
    PrecisionTensor enemy_lin_out;  // (B*8, 16) post-relu
    PrecisionTensor enemy_argmax;   // (B, 16) stored as precision_t
    PrecisionTensor enemy_out;      // (B, 16)
    PrecisionTensor concat_buf;     // (B, 112)
    PrecisionTensor out;            // (B, hidden)
    // Grad weights (only used in reg_train path)
    PrecisionTensor conv1_wgrad, conv1_bgrad;
    PrecisionTensor conv2_wgrad, conv2_bgrad;
    PrecisionTensor tile_wgrad, tile_bgrad;
    PrecisionTensor scalar_wgrad, scalar_bgrad;
    PrecisionTensor enemy_wgrad, enemy_bgrad;
    PrecisionTensor out_wgrad, out_bgrad;
    // Intermediate grad buffers
    PrecisionTensor g_tile_out;     // (B, 64)
    PrecisionTensor g_scalar_out;   // (B, 32)
    PrecisionTensor g_enemy_out;    // (B, 16)
    PrecisionTensor g_tile_flat;    // (B, 1280) = grad through conv2_out
    PrecisionTensor g_mp;           // (B, 16, 4, 10) = grad through mp_out
    PrecisionTensor g_conv1_out;    // (B, 16, 9, 21)
    PrecisionTensor g_enemy_lin;    // (B*8, 16)
};

// ---- im2col/col2im with zero padding ----

__global__ void im2col_pad_kernel(
    const precision_t* __restrict__ input, precision_t* __restrict__ col,
    int B, int IC, int IH, int IW, int K, int P, int OH, int OW
) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    int col_w = IC * K * K;
    if (idx >= B * OH * OW * col_w) return;
    int row = idx / col_w, c = idx % col_w;
    int b = row / (OH * OW), rem = row % (OH * OW);
    int oh = rem / OW, ow = rem % OW;
    int ic = c / (K * K), kk = c % (K * K);
    int kh = kk / K, kw = kk % K;
    int ih = oh + kh - P, iw = ow + kw - P;  // stride=1
    col[idx] = (ih < 0 || ih >= IH || iw < 0 || iw >= IW) ?
        from_float(0.0f) :
        input[b * IC * IH * IW + ic * IH * IW + ih * IW + iw];
}

__global__ void col2im_pad_kernel(
    const precision_t* __restrict__ col, precision_t* __restrict__ grad_in,
    int B, int IC, int IH, int IW, int K, int P, int OH, int OW
) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= B * IC * IH * IW) return;
    int iw = idx % IW, ih = (idx / IW) % IH;
    int ic = (idx / (IW * IH)) % IC, b = idx / (IW * IH * IC);
    float sum = 0.0f;
    for (int kh = 0; kh < K; kh++) {
        int oh = ih + P - kh;
        if (oh < 0 || oh >= OH) continue;
        for (int kw = 0; kw < K; kw++) {
            int ow = iw + P - kw;
            if (ow < 0 || ow >= OW) continue;
            int col_idx = (b * OH * OW + oh * OW + ow) * (IC * K * K) + ic * K * K + kh * K + kw;
            sum += to_float(col[col_idx]);
        }
    }
    grad_in[idx] = from_float(sum);
}

// conv forward with padding; col_buf/mm_buf are scratch (B*OH*OW, IC*K*K) and (B*OH*OW, OC)
static void t_conv_fwd(
    PrecisionTensor* w, PrecisionTensor* b_vec,
    precision_t* input, precision_t* output,
    precision_t* col, precision_t* mm,
    int B, int IC, int IH, int IW, int OC, int K, int P, int OH, int OW,
    bool relu, cudaStream_t stream
) {
    int rows = B * OH * OW, cols = IC * K * K;
    im2col_pad_kernel<<<grid_size(rows * cols), BLOCK_SIZE, 0, stream>>>(
        input, col, B, IC, IH, IW, K, P, OH, OW);
    PrecisionTensor col_t = {.data=col, .shape={rows, cols}};
    PrecisionTensor mm_t  = {.data=mm,  .shape={rows, OC}};
    puf_mm(&col_t, w, &mm_t, stream);
    int spatial = OH * OW;
    rows_to_nchw_kernel<<<grid_size(B*OC*spatial), BLOCK_SIZE, 0, stream>>>(
        mm, output, B, OC, spatial);
    if (relu)
        conv_bias_relu_kernel<<<grid_size(B*OC*spatial), BLOCK_SIZE, 0, stream>>>(
            output, b_vec->data, B, OC, spatial);
    else
        conv_bias_kernel<<<grid_size(B*OC*spatial), BLOCK_SIZE, 0, stream>>>(
            output, b_vec->data, B, OC, spatial);
}

// conv backward with padding; input_grad may be NULL (skip col2im)
static void t_conv_bwd(
    PrecisionTensor* w,
    precision_t* saved_input, precision_t* grad_out,
    precision_t* wgrad, precision_t* input_grad,
    precision_t* col, precision_t* mm,
    int B, int IC, int IH, int IW, int OC, int K, int P, int OH, int OW,
    cudaStream_t stream
) {
    int rows = B * OH * OW, col_w = IC * K * K;
    int spatial = OH * OW;
    nchw_to_rows_kernel<<<grid_size(B*OC*spatial), BLOCK_SIZE, 0, stream>>>(
        grad_out, mm, B, OC, spatial);
    im2col_pad_kernel<<<grid_size(rows * col_w), BLOCK_SIZE, 0, stream>>>(
        saved_input, col, B, IC, IH, IW, K, P, OH, OW);
    PrecisionTensor mm_t  = {.data=mm,  .shape={rows, OC}};
    PrecisionTensor col_t = {.data=col, .shape={rows, col_w}};
    PrecisionTensor wg_t  = {.data=wgrad, .shape={OC, col_w}};
    puf_mm_tn(&mm_t, &col_t, &wg_t, stream);
    if (input_grad) {
        puf_mm_nn(&mm_t, w, &col_t, stream);
        col2im_pad_kernel<<<grid_size(B*IC*IH*IW), BLOCK_SIZE, 0, stream>>>(
            col, input_grad, B, IC, IH, IW, K, P, OH, OW);
    }
}

// ---- Tile permute: (B, 9, 21, 2) → (B, 2, 9, 21) NCHW ----
__global__ void t_permute_tiles_kernel(
    const precision_t* __restrict__ obs, precision_t* __restrict__ out,
    int B, int obs_size
) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= B * T_IC * T_IH * T_IW) return;
    int b = idx / (T_IC * T_IH * T_IW);
    int rem = idx % (T_IC * T_IH * T_IW);
    int c = rem / (T_IH * T_IW);
    int hw = rem % (T_IH * T_IW);
    // source: (B, T_IH, T_IW, T_IC) layout at obs[0..378)
    int src = b * obs_size + hw * T_IC + c;
    out[idx] = obs[src];
}

// ---- Gather scalar features → (B, T_SCALAR_IN=98) ----
// Slices: [378:384](6), [384:386](2), [386:450](64), [450:456](6), [488:508](20)
__global__ void t_gather_scalar_kernel(
    const precision_t* __restrict__ obs, precision_t* __restrict__ out,
    int B, int obs_size
) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= B * T_SCALAR_IN) return;
    int b = idx / T_SCALAR_IN, d = idx % T_SCALAR_IN;
    int src_off;
    if      (d < 6)  src_off = 378 + d;
    else if (d < 8)  src_off = 384 + (d - 6);
    else if (d < 72) src_off = 386 + (d - 8);
    else if (d < 78) src_off = 450 + (d - 72);
    else             src_off = 488 + (d - 78);
    out[idx] = obs[b * obs_size + src_off];
}

// ---- Gather enemy features → (B*8, 4) ----
__global__ void t_gather_enemy_kernel(
    const precision_t* __restrict__ obs, precision_t* __restrict__ out,
    int B, int obs_size
) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= B * T_NEMY * T_ENEMY_IN) return;
    int b = idx / (T_NEMY * T_ENEMY_IN);
    int rem = idx % (T_NEMY * T_ENEMY_IN);
    // obs[456:488] = 8 enemies * 4 features
    out[idx] = obs[b * obs_size + 456 + rem];
}

// ---- MaxPool2d(2) forward (NCHW), optional argmax for backward ----
__global__ void t_maxpool_fwd_kernel(
    const precision_t* __restrict__ input, precision_t* __restrict__ output,
    precision_t* argmax,
    int B, int C, int IH, int IW, int OH, int OW
) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= B * C * OH * OW) return;
    int ow = idx % OW, oh = (idx / OW) % OH;
    int c = (idx / (OW * OH)) % C, b = idx / (OW * OH * C);
    float mx = -1e38f; int mi = 0;
    for (int kh = 0; kh < 2; kh++) {
        for (int kw = 0; kw < 2; kw++) {
            int ih = oh * 2 + kh, iw = ow * 2 + kw;
            if (ih < IH && iw < IW) {
                float v = to_float(input[((b * C + c) * IH + ih) * IW + iw]);
                if (v > mx) { mx = v; mi = ih * IW + iw; }
            }
        }
    }
    output[idx] = from_float(mx);
    if (argmax) argmax[idx] = from_float((float)mi);
}

// ---- MaxPool2d(2) backward ----
__global__ void t_maxpool_bwd_kernel(
    const precision_t* __restrict__ argmax,
    const precision_t* __restrict__ grad_out,
    precision_t* __restrict__ grad_in,
    int B, int C, int IH, int IW, int OH, int OW
) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= B * C * OH * OW) return;
    int c = (idx / (OW * OH)) % C, b = idx / (OW * OH * C);
    int spatial_idx = (int)to_float(argmax[idx]);
    int ih = spatial_idx / IW, iw = spatial_idx % IW;
    int in_idx = ((b * C + c) * IH + ih) * IW + iw;
    atomicAdd_precision(&grad_in[in_idx], grad_out[idx]);
}

// ---- Enemy: linear(4→16) output (B*8,16) → relu + maxpool → (B,16), save argmax ----
__global__ void t_enemy_relu_maxpool_kernel(
    precision_t* __restrict__ lin_out,  // (B*8, 16) in-place relu, then maxpool
    precision_t* __restrict__ pool_out, // (B, 16)
    precision_t* __restrict__ argmax,   // (B, 16) argmax over 8 enemies
    const precision_t* __restrict__ bias,
    int B
) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= B * T_ENEMY_OUT) return;
    int b = idx / T_ENEMY_OUT, d = idx % T_ENEMY_OUT;
    float mx = -1e38f; int mi = 0;
    for (int k = 0; k < T_NEMY; k++) {
        int li = (b * T_NEMY + k) * T_ENEMY_OUT + d;
        float v = fmaxf(0.0f, to_float(lin_out[li]) + to_float(bias[d]));
        lin_out[li] = from_float(v);
        if (v > mx) { mx = v; mi = k; }
    }
    pool_out[idx] = from_float(mx);
    if (argmax) argmax[idx] = from_float((float)mi);
}

// ---- Enemy maxpool backward: scatter grad back to (B*8,16) + apply relu gate ----
__global__ void t_enemy_maxpool_bwd_kernel(
    const precision_t* __restrict__ grad_out,    // (B, 16)
    const precision_t* __restrict__ argmax,       // (B, 16)
    const precision_t* __restrict__ lin_out,      // (B*8, 16) post-relu values
    precision_t* __restrict__ grad_lin,            // (B*8, 16) output
    int B
) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= B * T_NEMY * T_ENEMY_OUT) return;
    int b = idx / (T_NEMY * T_ENEMY_OUT);
    int k = (idx / T_ENEMY_OUT) % T_NEMY;
    int d = idx % T_ENEMY_OUT;
    int pool_idx = b * T_ENEMY_OUT + d;
    int am = (int)to_float(argmax[pool_idx]);
    float g = (k == am) ? to_float(grad_out[pool_idx]) : 0.0f;
    // relu gate: lin_out stores post-relu; 0 means pre-relu was ≤ 0
    g *= (to_float(lin_out[idx]) > 0.0f) ? 1.0f : 0.0f;
    grad_lin[idx] = from_float(g);
}

// ---- Concatenate tile(64)+scalar(32)+enemy(16) → (B,112) ----
__global__ void t_concat_kernel(
    const precision_t* __restrict__ tile,
    const precision_t* __restrict__ scalar,
    const precision_t* __restrict__ enemy,
    precision_t* __restrict__ out, int B
) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= B * T_CONCAT_IN) return;
    int b = idx / T_CONCAT_IN, d = idx % T_CONCAT_IN;
    precision_t v;
    if      (d < T_TILE_OUT)                    v = tile  [b * T_TILE_OUT   + d];
    else if (d < T_TILE_OUT + T_SCALAR_OUT)     v = scalar[b * T_SCALAR_OUT + (d - T_TILE_OUT)];
    else                                         v = enemy [b * T_ENEMY_OUT  + (d - T_TILE_OUT - T_SCALAR_OUT)];
    out[idx] = v;
}

// ---- Split concat grad → tile/scalar/enemy parts ----
__global__ void t_concat_bwd_kernel(
    const precision_t* __restrict__ grad,
    precision_t* __restrict__ g_tile,
    precision_t* __restrict__ g_scalar,
    precision_t* __restrict__ g_enemy,
    int B
) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= B * T_CONCAT_IN) return;
    int b = idx / T_CONCAT_IN, d = idx % T_CONCAT_IN;
    precision_t v = grad[idx];
    if      (d < T_TILE_OUT)                    g_tile  [b * T_TILE_OUT   + d] = v;
    else if (d < T_TILE_OUT + T_SCALAR_OUT)     g_scalar[b * T_SCALAR_OUT + (d - T_TILE_OUT)] = v;
    else                                         g_enemy [b * T_ENEMY_OUT  + (d - T_TILE_OUT - T_SCALAR_OUT)] = v;
}

// ---- Encoder forward ----
static PrecisionTensor terraria_encoder_forward(
    void* w, void* activations, PrecisionTensor input, cudaStream_t stream
) {
    TerrariaEncoderWeights* ew = (TerrariaEncoderWeights*)w;
    TerrariaEncoderActivations* a = (TerrariaEncoderActivations*)activations;
    int B = input.shape[0];
    int obs = input.shape[1];

    // Permute tile data (B,9,21,2)→(B,2,9,21)
    t_permute_tiles_kernel<<<grid_size(B * T_IC * T_IH * T_IW), BLOCK_SIZE, 0, stream>>>(
        input.data, a->tiles_nchw.data, B, obs);

    // Conv1 + relu
    t_conv_fwd(&ew->conv1_w, &ew->conv1_b, a->tiles_nchw.data, a->conv1_out.data,
        a->col1.data, a->mm1.data, B, T_IC, T_IH, T_IW, T_C1_OC, T_C1_K, T_C1_P,
        T_C1_OH, T_C1_OW, true, stream);

    // MaxPool2d
    t_maxpool_fwd_kernel<<<grid_size(B * T_C1_OC * T_MP_OH * T_MP_OW), BLOCK_SIZE, 0, stream>>>(
        a->conv1_out.data, a->mp_out.data, a->mp_argmax.data,
        B, T_C1_OC, T_C1_OH, T_C1_OW, T_MP_OH, T_MP_OW);

    // Conv2 + relu
    t_conv_fwd(&ew->conv2_w, &ew->conv2_b, a->mp_out.data, a->conv2_out.data,
        a->col2.data, a->mm2.data, B, T_C2_IC, T_MP_OH, T_MP_OW, T_C2_OC, T_C2_K, T_C2_P,
        T_C2_OH, T_C2_OW, true, stream);

    // Tile linear: conv2_out (B,1280) → tile_out (B,64) + bias + relu
    PrecisionTensor flat = {.data=a->conv2_out.data, .shape={B, T_FLAT}};
    puf_mm(&flat, &ew->tile_w, &a->tile_out, stream);
    n3_bias_relu_kernel<<<grid_size(B * T_TILE_OUT), BLOCK_SIZE, 0, stream>>>(
        a->tile_out.data, ew->tile_b.data, B * T_TILE_OUT, T_TILE_OUT);

    // Scalar branch
    t_gather_scalar_kernel<<<grid_size(B * T_SCALAR_IN), BLOCK_SIZE, 0, stream>>>(
        input.data, a->scalar_buf.data, B, obs);
    puf_mm(&a->scalar_buf, &ew->scalar_w, &a->scalar_out, stream);
    n3_bias_relu_kernel<<<grid_size(B * T_SCALAR_OUT), BLOCK_SIZE, 0, stream>>>(
        a->scalar_out.data, ew->scalar_b.data, B * T_SCALAR_OUT, T_SCALAR_OUT);

    // Enemy branch: gather → linear → relu+maxpool
    t_gather_enemy_kernel<<<grid_size(B * T_NEMY * T_ENEMY_IN), BLOCK_SIZE, 0, stream>>>(
        input.data, a->enemy_flat.data, B, obs);
    puf_mm(&a->enemy_flat, &ew->enemy_w, &a->enemy_lin_out, stream);
    t_enemy_relu_maxpool_kernel<<<grid_size(B * T_ENEMY_OUT), BLOCK_SIZE, 0, stream>>>(
        a->enemy_lin_out.data, a->enemy_out.data, a->enemy_argmax.data, ew->enemy_b.data, B);

    // Concat + out projection + relu
    t_concat_kernel<<<grid_size(B * T_CONCAT_IN), BLOCK_SIZE, 0, stream>>>(
        a->tile_out.data, a->scalar_out.data, a->enemy_out.data, a->concat_buf.data, B);
    puf_mm(&a->concat_buf, &ew->out_w, &a->out, stream);
    n3_bias_relu_kernel<<<grid_size(B * ew->hidden), BLOCK_SIZE, 0, stream>>>(
        a->out.data, ew->out_b.data, B * ew->hidden, ew->hidden);
    return a->out;
}

// ---- Encoder backward (computes weight grads; no need to backprop to obs) ----
static void terraria_encoder_backward(
    void* w, void* activations, PrecisionTensor grad, cudaStream_t stream
) {
    TerrariaEncoderWeights* ew = (TerrariaEncoderWeights*)w;
    TerrariaEncoderActivations* a = (TerrariaEncoderActivations*)activations;
    int B = grad.shape[0], H = ew->hidden;

    // Out layer backward
    n3_relu_backward_kernel<<<grid_size(B * H), BLOCK_SIZE, 0, stream>>>(
        grad.data, a->out.data, B * H);
    bias_grad_kernel<<<H, 256, 0, stream>>>(a->out_bgrad.data, grad.data, B, H);
    puf_mm_tn(&grad, &a->concat_buf, &a->out_wgrad, stream);

    // Reuse concat_buf for grad (forward value already consumed by out_wgrad above)
    PrecisionTensor g_concat = {.data=a->concat_buf.data, .shape={B, T_CONCAT_IN}};
    puf_mm_nn(&grad, &ew->out_w, &g_concat, stream);

    // Split concat grad
    t_concat_bwd_kernel<<<grid_size(B * T_CONCAT_IN), BLOCK_SIZE, 0, stream>>>(
        g_concat.data, a->g_tile_out.data, a->g_scalar_out.data, a->g_enemy_out.data, B);

    // --- Tile branch backward ---
    // relu backward on tile_out
    n3_relu_backward_kernel<<<grid_size(B * T_TILE_OUT), BLOCK_SIZE, 0, stream>>>(
        a->g_tile_out.data, a->tile_out.data, B * T_TILE_OUT);
    bias_grad_kernel<<<T_TILE_OUT, 256, 0, stream>>>(
        a->tile_bgrad.data, a->g_tile_out.data, B, T_TILE_OUT);
    PrecisionTensor flat = {.data=a->conv2_out.data, .shape={B, T_FLAT}};
    puf_mm_tn(&a->g_tile_out, &flat, &a->tile_wgrad, stream);
    // grad through tile_flat = conv2_out
    puf_mm_nn(&a->g_tile_out, &ew->tile_w, &a->g_tile_flat, stream);
    // relu backward on conv2_out (g_tile_flat is grad w.r.t. conv2_out)
    n3_relu_backward_kernel<<<grid_size(B * T_FLAT), BLOCK_SIZE, 0, stream>>>(
        a->g_tile_flat.data, a->conv2_out.data, B * T_FLAT);
    // conv2 bias grad
    n3_conv_bias_grad_nchw<<<T_C2_OC, 256, 0, stream>>>(
        a->conv2_bgrad.data, a->g_tile_flat.data, B, T_C2_OC, T_C2_OH * T_C2_OW);
    // conv2 weight grad + grad through mp_out
    t_conv_bwd(&ew->conv2_w,
        a->mp_out.data, a->g_tile_flat.data,
        a->conv2_wgrad.data, a->g_mp.data,
        a->col2.data, a->mm2.data,
        B, T_C2_IC, T_MP_OH, T_MP_OW, T_C2_OC, T_C2_K, T_C2_P, T_C2_OH, T_C2_OW, stream);
    // maxpool backward
    cudaMemsetAsync(a->g_conv1_out.data, 0,
        (int64_t)B * T_C1_OC * T_C1_OH * T_C1_OW * sizeof(precision_t), stream);
    t_maxpool_bwd_kernel<<<grid_size(B * T_C1_OC * T_MP_OH * T_MP_OW), BLOCK_SIZE, 0, stream>>>(
        a->mp_argmax.data, a->g_mp.data, a->g_conv1_out.data,
        B, T_C1_OC, T_C1_OH, T_C1_OW, T_MP_OH, T_MP_OW);
    // relu backward on conv1_out
    n3_relu_backward_kernel<<<grid_size(B * T_C1_OC * T_C1_OH * T_C1_OW), BLOCK_SIZE, 0, stream>>>(
        a->g_conv1_out.data, a->conv1_out.data, B * T_C1_OC * T_C1_OH * T_C1_OW);
    // conv1 bias grad
    n3_conv_bias_grad_nchw<<<T_C1_OC, 256, 0, stream>>>(
        a->conv1_bgrad.data, a->g_conv1_out.data, B, T_C1_OC, T_C1_OH * T_C1_OW);
    // conv1 weight grad only (no need to backprop to tiles = obs input)
    t_conv_bwd(&ew->conv1_w,
        a->tiles_nchw.data, a->g_conv1_out.data,
        a->conv1_wgrad.data, nullptr,
        a->col1.data, a->mm1.data,
        B, T_IC, T_IH, T_IW, T_C1_OC, T_C1_K, T_C1_P, T_C1_OH, T_C1_OW, stream);

    // --- Scalar branch backward ---
    n3_relu_backward_kernel<<<grid_size(B * T_SCALAR_OUT), BLOCK_SIZE, 0, stream>>>(
        a->g_scalar_out.data, a->scalar_out.data, B * T_SCALAR_OUT);
    bias_grad_kernel<<<T_SCALAR_OUT, 256, 0, stream>>>(
        a->scalar_bgrad.data, a->g_scalar_out.data, B, T_SCALAR_OUT);
    puf_mm_tn(&a->g_scalar_out, &a->scalar_buf, &a->scalar_wgrad, stream);

    // --- Enemy branch backward ---
    t_enemy_maxpool_bwd_kernel<<<grid_size(B * T_NEMY * T_ENEMY_OUT), BLOCK_SIZE, 0, stream>>>(
        a->g_enemy_out.data, a->enemy_argmax.data, a->enemy_lin_out.data, a->g_enemy_lin.data, B);
    bias_grad_kernel<<<T_ENEMY_OUT, 256, 0, stream>>>(
        a->enemy_bgrad.data, a->g_enemy_lin.data, B * T_NEMY, T_ENEMY_OUT);
    puf_mm_tn(&a->g_enemy_lin, &a->enemy_flat, &a->enemy_wgrad, stream);
}

// ---- Weight init ----
static void terraria_encoder_init_weights(void* w, uint64_t* seed, cudaStream_t stream) {
    TerrariaEncoderWeights* ew = (TerrariaEncoderWeights*)w;
    auto ki = [&](PrecisionTensor& t, int r, int c) {
        PrecisionTensor wt = {.data=t.data, .shape={r, c}};
        puf_kaiming_init(&wt, std::sqrt(2.0f), (*seed)++, stream);
    };
    ki(ew->conv1_w, T_C1_OC, T_IC * T_C1_K * T_C1_K);
    ki(ew->conv2_w, T_C2_OC, T_C2_IC * T_C2_K * T_C2_K);
    ki(ew->tile_w,  T_TILE_OUT, T_FLAT);
    ki(ew->scalar_w, T_SCALAR_OUT, T_SCALAR_IN);
    ki(ew->enemy_w,  T_ENEMY_OUT, T_ENEMY_IN);
    ki(ew->out_w, ew->hidden, T_CONCAT_IN);
    // Zero all biases
    auto zb = [&](PrecisionTensor& b_vec, int n) {
        cudaMemsetAsync(b_vec.data, 0, n * sizeof(precision_t), stream);
    };
    zb(ew->conv1_b, T_C1_OC); zb(ew->conv2_b, T_C2_OC);
    zb(ew->tile_b, T_TILE_OUT); zb(ew->scalar_b, T_SCALAR_OUT);
    zb(ew->enemy_b, T_ENEMY_OUT); zb(ew->out_b, ew->hidden);
}

// ---- Param registration ----
static void terraria_encoder_reg_params(void* w, Allocator* alloc) {
    TerrariaEncoderWeights* ew = (TerrariaEncoderWeights*)w;
    ew->conv1_w = {.shape={T_C1_OC, T_IC*T_C1_K*T_C1_K}};
    ew->conv1_b = {.shape={T_C1_OC}};
    ew->conv2_w = {.shape={T_C2_OC, T_C2_IC*T_C2_K*T_C2_K}};
    ew->conv2_b = {.shape={T_C2_OC}};
    ew->tile_w  = {.shape={T_TILE_OUT, T_FLAT}};
    ew->tile_b  = {.shape={T_TILE_OUT}};
    ew->scalar_w = {.shape={T_SCALAR_OUT, T_SCALAR_IN}};
    ew->scalar_b = {.shape={T_SCALAR_OUT}};
    ew->enemy_w  = {.shape={T_ENEMY_OUT, T_ENEMY_IN}};
    ew->enemy_b  = {.shape={T_ENEMY_OUT}};
    ew->out_w = {.shape={ew->hidden, T_CONCAT_IN}};
    ew->out_b = {.shape={ew->hidden}};
    alloc_register(alloc, &ew->conv1_w); alloc_register(alloc, &ew->conv1_b);
    alloc_register(alloc, &ew->conv2_w); alloc_register(alloc, &ew->conv2_b);
    alloc_register(alloc, &ew->tile_w);  alloc_register(alloc, &ew->tile_b);
    alloc_register(alloc, &ew->scalar_w); alloc_register(alloc, &ew->scalar_b);
    alloc_register(alloc, &ew->enemy_w);  alloc_register(alloc, &ew->enemy_b);
    alloc_register(alloc, &ew->out_w); alloc_register(alloc, &ew->out_b);
}

// ---- Training activation registration ----
static void terraria_encoder_reg_train(
    void* w, void* activations, Allocator* acts, Allocator* grads, int B_TT
) {
    TerrariaEncoderWeights* ew = (TerrariaEncoderWeights*)w;
    TerrariaEncoderActivations* a = (TerrariaEncoderActivations*)activations;
    *a = {};
    int B = B_TT;
    // Forward activations
    a->tiles_nchw  = {.shape={B, T_IC*T_IH*T_IW}};
    a->conv1_out   = {.shape={B, T_C1_OC*T_C1_OH*T_C1_OW}};
    a->mp_out      = {.shape={B, T_C1_OC*T_MP_OH*T_MP_OW}};
    a->mp_argmax   = {.shape={B, T_C1_OC*T_MP_OH*T_MP_OW}};
    a->conv2_out   = {.shape={B, T_FLAT}};
    a->tile_out    = {.shape={B, T_TILE_OUT}};
    a->col1 = {.shape={B*T_C1_OH*T_C1_OW, T_IC*T_C1_K*T_C1_K}};
    a->mm1  = {.shape={B*T_C1_OH*T_C1_OW, T_C1_OC}};
    a->col2 = {.shape={B*T_C2_OH*T_C2_OW, T_C2_IC*T_C2_K*T_C2_K}};
    a->mm2  = {.shape={B*T_C2_OH*T_C2_OW, T_C2_OC}};
    a->scalar_buf  = {.shape={B, T_SCALAR_IN}};
    a->scalar_out  = {.shape={B, T_SCALAR_OUT}};
    a->enemy_flat  = {.shape={B*T_NEMY, T_ENEMY_IN}};
    a->enemy_lin_out = {.shape={B*T_NEMY, T_ENEMY_OUT}};
    a->enemy_argmax  = {.shape={B, T_ENEMY_OUT}};
    a->enemy_out   = {.shape={B, T_ENEMY_OUT}};
    a->concat_buf  = {.shape={B, T_CONCAT_IN}};
    a->out         = {.shape={B, ew->hidden}};
    // Grad buffers
    a->g_tile_out   = {.shape={B, T_TILE_OUT}};
    a->g_scalar_out = {.shape={B, T_SCALAR_OUT}};
    a->g_enemy_out  = {.shape={B, T_ENEMY_OUT}};
    a->g_tile_flat  = {.shape={B, T_FLAT}};
    a->g_mp         = {.shape={B, T_C1_OC*T_MP_OH*T_MP_OW}};
    a->g_conv1_out  = {.shape={B, T_C1_OC*T_C1_OH*T_C1_OW}};
    a->g_enemy_lin  = {.shape={B*T_NEMY, T_ENEMY_OUT}};
    // Weight grad tensors
    a->conv1_wgrad = {.shape={T_C1_OC, T_IC*T_C1_K*T_C1_K}};
    a->conv1_bgrad = {.shape={T_C1_OC}};
    a->conv2_wgrad = {.shape={T_C2_OC, T_C2_IC*T_C2_K*T_C2_K}};
    a->conv2_bgrad = {.shape={T_C2_OC}};
    a->tile_wgrad  = {.shape={T_TILE_OUT, T_FLAT}};
    a->tile_bgrad  = {.shape={T_TILE_OUT}};
    a->scalar_wgrad = {.shape={T_SCALAR_OUT, T_SCALAR_IN}};
    a->scalar_bgrad = {.shape={T_SCALAR_OUT}};
    a->enemy_wgrad  = {.shape={T_ENEMY_OUT, T_ENEMY_IN}};
    a->enemy_bgrad  = {.shape={T_ENEMY_OUT}};
    a->out_wgrad = {.shape={ew->hidden, T_CONCAT_IN}};
    a->out_bgrad = {.shape={ew->hidden}};
    // Register forward activations
    alloc_register(acts, &a->tiles_nchw);  alloc_register(acts, &a->conv1_out);
    alloc_register(acts, &a->mp_out);      alloc_register(acts, &a->mp_argmax);
    alloc_register(acts, &a->conv2_out);   alloc_register(acts, &a->tile_out);
    alloc_register(acts, &a->col1);        alloc_register(acts, &a->mm1);
    alloc_register(acts, &a->col2);        alloc_register(acts, &a->mm2);
    alloc_register(acts, &a->scalar_buf);  alloc_register(acts, &a->scalar_out);
    alloc_register(acts, &a->enemy_flat);  alloc_register(acts, &a->enemy_lin_out);
    alloc_register(acts, &a->enemy_argmax); alloc_register(acts, &a->enemy_out);
    alloc_register(acts, &a->concat_buf);  alloc_register(acts, &a->out);
    // Register grad buffers
    alloc_register(acts, &a->g_tile_out);  alloc_register(acts, &a->g_scalar_out);
    alloc_register(acts, &a->g_enemy_out); alloc_register(acts, &a->g_tile_flat);
    alloc_register(acts, &a->g_mp);        alloc_register(acts, &a->g_conv1_out);
    alloc_register(acts, &a->g_enemy_lin);
    // Register weight grads
    alloc_register(grads, &a->conv1_wgrad); alloc_register(grads, &a->conv1_bgrad);
    alloc_register(grads, &a->conv2_wgrad); alloc_register(grads, &a->conv2_bgrad);
    alloc_register(grads, &a->tile_wgrad);  alloc_register(grads, &a->tile_bgrad);
    alloc_register(grads, &a->scalar_wgrad); alloc_register(grads, &a->scalar_bgrad);
    alloc_register(grads, &a->enemy_wgrad);  alloc_register(grads, &a->enemy_bgrad);
    alloc_register(grads, &a->out_wgrad);   alloc_register(grads, &a->out_bgrad);
}

// ---- Rollout activation registration (forward only, no grads) ----
static void terraria_encoder_reg_rollout(
    void* w, void* activations, Allocator* alloc, int B
) {
    TerrariaEncoderWeights* ew = (TerrariaEncoderWeights*)w;
    TerrariaEncoderActivations* a = (TerrariaEncoderActivations*)activations;
    *a = {};
    a->tiles_nchw  = {.shape={B, T_IC*T_IH*T_IW}};
    a->conv1_out   = {.shape={B, T_C1_OC*T_C1_OH*T_C1_OW}};
    a->mp_out      = {.shape={B, T_C1_OC*T_MP_OH*T_MP_OW}};
    // No mp_argmax needed for inference
    a->conv2_out   = {.shape={B, T_FLAT}};
    a->tile_out    = {.shape={B, T_TILE_OUT}};
    a->col1 = {.shape={B*T_C1_OH*T_C1_OW, T_IC*T_C1_K*T_C1_K}};
    a->mm1  = {.shape={B*T_C1_OH*T_C1_OW, T_C1_OC}};
    a->col2 = {.shape={B*T_C2_OH*T_C2_OW, T_C2_IC*T_C2_K*T_C2_K}};
    a->mm2  = {.shape={B*T_C2_OH*T_C2_OW, T_C2_OC}};
    a->scalar_buf  = {.shape={B, T_SCALAR_IN}};
    a->scalar_out  = {.shape={B, T_SCALAR_OUT}};
    a->enemy_flat  = {.shape={B*T_NEMY, T_ENEMY_IN}};
    a->enemy_lin_out = {.shape={B*T_NEMY, T_ENEMY_OUT}};
    // No enemy_argmax for inference
    a->enemy_out   = {.shape={B, T_ENEMY_OUT}};
    a->concat_buf  = {.shape={B, T_CONCAT_IN}};
    a->out         = {.shape={B, ew->hidden}};
    alloc_register(alloc, &a->tiles_nchw);  alloc_register(alloc, &a->conv1_out);
    alloc_register(alloc, &a->mp_out);
    alloc_register(alloc, &a->conv2_out);   alloc_register(alloc, &a->tile_out);
    alloc_register(alloc, &a->col1);        alloc_register(alloc, &a->mm1);
    alloc_register(alloc, &a->col2);        alloc_register(alloc, &a->mm2);
    alloc_register(alloc, &a->scalar_buf);  alloc_register(alloc, &a->scalar_out);
    alloc_register(alloc, &a->enemy_flat);  alloc_register(alloc, &a->enemy_lin_out);
    alloc_register(alloc, &a->enemy_out);
    alloc_register(alloc, &a->concat_buf);  alloc_register(alloc, &a->out);
}

static void* terraria_encoder_create_weights(void* self) {
    Encoder* e = (Encoder*)self;
    TerrariaEncoderWeights* ew = (TerrariaEncoderWeights*)calloc(1, sizeof(TerrariaEncoderWeights));
    ew->hidden = e->out_dim;
    return ew;
}
static void terraria_encoder_free_weights(void* w) { free(w); }
static void terraria_encoder_free_activations(void* a) { free(a); }

// Override encoder vtable for known ocean environments. No-op for unknown envs.
static void create_custom_encoder(const std::string& env_name, Encoder* enc) {
    if (env_name == "nmmo3") {
        *enc = Encoder{
            .forward = nmmo3_encoder_forward,
            .backward = nmmo3_encoder_backward,
            .init_weights = nmmo3_encoder_init_weights,
            .reg_params = nmmo3_encoder_reg_params,
            .reg_train = nmmo3_encoder_reg_train,
            .reg_rollout = nmmo3_encoder_reg_rollout,
            .create_weights = nmmo3_encoder_create_weights,
            .free_weights = nmmo3_encoder_free_weights,
            .free_activations = nmmo3_encoder_free_activations,
            .in_dim = enc->in_dim, .out_dim = enc->out_dim,
            .activation_size = sizeof(NMMO3EncoderActivations),
        };
    }
    if (env_name == "terraria") {
        *enc = Encoder{
            .forward = terraria_encoder_forward,
            .backward = terraria_encoder_backward,
            .init_weights = terraria_encoder_init_weights,
            .reg_params = terraria_encoder_reg_params,
            .reg_train = terraria_encoder_reg_train,
            .reg_rollout = terraria_encoder_reg_rollout,
            .create_weights = terraria_encoder_create_weights,
            .free_weights = terraria_encoder_free_weights,
            .free_activations = terraria_encoder_free_activations,
            .in_dim = enc->in_dim, .out_dim = enc->out_dim,
            .activation_size = sizeof(TerrariaEncoderActivations),
        };
    }
}
