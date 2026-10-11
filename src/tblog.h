#pragma once

// Minimal TensorBoard event-file writer: TFRecord framing around hand-encoded
// tensorflow.Event protobufs. Scalars (simple_value) and text-plugin strings.

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/time.h>

#include "ini.h"

typedef struct {
    FILE* fp;
    uint8_t* buf;
    size_t len;
    size_t cap;
} TbLog;

static uint32_t tb_crc_table[256];

// CRC32C (Castagnoli), reflected poly 0x82F63B78.
static inline uint32_t tb_crc32c(const uint8_t* p, size_t n) {
    if (!tb_crc_table[1]) {
        for (uint32_t i = 0; i < 256; i++) {
            uint32_t c = i;
            for (int k = 0; k < 8; k++) {
                c = c & 1 ? (c >> 1) ^ 0x82F63B78u : c >> 1;
            }
            tb_crc_table[i] = c;
        }
    }
    uint32_t c = 0xFFFFFFFFu;
    for (size_t i = 0; i < n; i++) {
        c = tb_crc_table[(c ^ p[i]) & 0xFF] ^ (c >> 8);
    }
    return c ^ 0xFFFFFFFFu;
}

static inline uint32_t tb_masked_crc(const uint8_t* p, size_t n) {
    uint32_t c = tb_crc32c(p, n);
    return ((c >> 15) | (c << 17)) + 0xA282EAD8u;
}

static inline void tb_put(TbLog* tb, const void* p, size_t n) {
    if (tb->len + n > tb->cap) {
        tb->cap = (tb->len + n) * 2;
        tb->buf = (uint8_t*)realloc(tb->buf, tb->cap);
    }
    memcpy(tb->buf + tb->len, p, n);
    tb->len += n;
}

static inline int tb_varint_len(uint64_t v) {
    int n = 1;
    while (v >= 0x80) {
        v >>= 7;
        n++;
    }
    return n;
}

static inline void tb_varint(TbLog* tb, uint64_t v) {
    uint8_t b[10];
    int n = 0;
    while (v >= 0x80) {
        b[n++] = (uint8_t)(v | 0x80);
        v >>= 7;
    }
    b[n++] = (uint8_t)v;
    tb_put(tb, b, n);
}

static inline void tb_key(TbLog* tb, int field, int wire) {
    tb_varint(tb, (uint64_t)(field << 3 | wire));
}

static inline void tb_bytes(TbLog* tb, int field, const void* p, size_t n) {
    tb_key(tb, field, 2);
    tb_varint(tb, n);
    tb_put(tb, p, n);
}

// Event header (wall_time=1, step=2) into a fresh buffer.
static inline void tb_event_begin(TbLog* tb, long step) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    double wall = tv.tv_sec + tv.tv_usec * 1e-6;
    tb->len = 0;
    tb_key(tb, 1, 1);
    tb_put(tb, &wall, 8);
    tb_key(tb, 2, 0);
    tb_varint(tb, (uint64_t)step);
}

static inline void tb_record(TbLog* tb) {
    uint64_t n = tb->len;
    uint32_t len_crc = tb_masked_crc((const uint8_t*)&n, 8);
    uint32_t data_crc = tb_masked_crc(tb->buf, tb->len);
    fwrite(&n, 8, 1, tb->fp);
    fwrite(&len_crc, 4, 1, tb->fp);
    fwrite(tb->buf, 1, tb->len, tb->fp);
    fwrite(&data_crc, 4, 1, tb->fp);
    fflush(tb->fp);
}

static inline int tb_open(TbLog* tb, const char* dir) {
    char host[256] = "localhost";
    gethostname(host, sizeof(host) - 1);
    char path[4096];
    snprintf(path, sizeof(path), "%s/events.out.tfevents.%ld.%s",
        dir, (long)time(NULL), host);
    memset(tb, 0, sizeof(*tb));
    tb->fp = fopen(path, "wb");
    if (!tb->fp) {
        return 0;
    }
    tb_event_begin(tb, 0);
    const char* version = "brain.Event:2";
    tb_bytes(tb, 3, version, strlen(version));
    tb_record(tb);
    return 1;
}

// One Event with a Summary.Value(tag, simple_value) per finite numeric key.
static inline void tb_scalars(TbLog* tb, long step, Dict* log) {
    tb_event_begin(tb, step);
    size_t summary_len = 0;
    for (int i = 0; i < log->size; i++) {
        DictItem* it = &log->items[i];
        if (it->str || it->len || !isfinite(it->value)) {
            continue;
        }
        size_t tag = strlen(it->key);
        size_t val = 1 + tb_varint_len(tag) + tag + 5;
        summary_len += 1 + tb_varint_len(val) + val;
    }
    tb_key(tb, 5, 2);
    tb_varint(tb, summary_len);
    for (int i = 0; i < log->size; i++) {
        DictItem* it = &log->items[i];
        if (it->str || it->len || !isfinite(it->value)) {
            continue;
        }
        size_t tag = strlen(it->key);
        float v = (float)it->value;
        tb_key(tb, 1, 2);
        tb_varint(tb, 1 + tb_varint_len(tag) + tag + 5);
        tb_bytes(tb, 1, it->key, tag);
        tb_key(tb, 2, 5);
        tb_put(tb, &v, 4);
    }
    tb_record(tb);
}

// Text-plugin summary: Value{tag, metadata{plugin_data{"text"}}, tensor{DT_STRING, string_val}}.
static inline void tb_text(TbLog* tb, long step, const char* tag, const char* text) {
    size_t tag_n = strlen(tag), text_n = strlen(text);
    size_t plugin = 1 + 1 + 4;
    size_t meta = 1 + tb_varint_len(plugin) + plugin;
    size_t tensor = 2 + 1 + tb_varint_len(text_n) + text_n;
    size_t val = 1 + tb_varint_len(tag_n) + tag_n
        + 1 + tb_varint_len(meta) + meta
        + 1 + tb_varint_len(tensor) + tensor;
    size_t summary = 1 + tb_varint_len(val) + val;
    tb_event_begin(tb, step);
    tb_key(tb, 5, 2);
    tb_varint(tb, summary);
    tb_key(tb, 1, 2);
    tb_varint(tb, val);
    tb_bytes(tb, 1, tag, tag_n);
    tb_key(tb, 9, 2);
    tb_varint(tb, meta);
    tb_key(tb, 1, 2);
    tb_varint(tb, plugin);
    tb_bytes(tb, 1, "text", 4);
    tb_key(tb, 8, 2);
    tb_varint(tb, tensor);
    tb_key(tb, 1, 0);
    tb_varint(tb, 7);
    tb_bytes(tb, 8, text, text_n);
    tb_record(tb);
}

// Submessages are built in scratch TbLogs (fp unused) and emitted as bytes.
static inline void tb_msg(TbLog* tb, int field, TbLog* sub) {
    tb_bytes(tb, field, sub->buf, sub->len);
    sub->len = 0;
}

static inline void tb_double(TbLog* tb, int field, double v) {
    tb_key(tb, field, 1);
    tb_put(tb, &v, 8);
}

static inline double tb_now(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec + tv.tv_usec * 1e-6;
}

// HParams plugin: Value{tag, metadata{plugin_data{"hparams", HParamsPluginData}}}.
// TensorBoard's loader fills in the missing tensor.
static inline void tb_hparams_event(TbLog* tb, const char* tag, TbLog* data) {
    TbLog plugin = {0}, meta = {0}, val = {0}, summary = {0};
    tb_bytes(&plugin, 1, "hparams", 7);
    tb_bytes(&plugin, 2, data->buf, data->len);
    tb_msg(&meta, 1, &plugin);
    tb_bytes(&val, 1, tag, strlen(tag));
    tb_msg(&val, 9, &meta);
    tb_msg(&summary, 1, &val);
    tb_event_begin(tb, 0);
    tb_msg(tb, 5, &summary);
    tb_record(tb);
    free(plugin.buf);
    free(meta.buf);
    free(val.buf);
    free(summary.buf);
}

// Experiment{hparam_infos(FLOAT64, interval), metric_infos{name{tag}}}.
static inline void tb_hparams_experiment(TbLog* tb, const char** names,
        const double* mins, const double* maxs, int n, const char** metrics, int n_metrics) {
    TbLog exp = {0}, info = {0}, sub = {0}, data = {0};
    for (int i = 0; i < n; i++) {
        tb_bytes(&info, 1, names[i], strlen(names[i]));
        tb_key(&info, 4, 0);
        tb_varint(&info, 3);
        if (mins[i] < maxs[i]) {
            tb_double(&sub, 1, mins[i]);
            tb_double(&sub, 2, maxs[i]);
            tb_msg(&info, 6, &sub);
        }
        tb_msg(&exp, 4, &info);
    }
    for (int i = 0; i < n_metrics; i++) {
        tb_bytes(&sub, 2, metrics[i], strlen(metrics[i]));
        tb_msg(&info, 1, &sub);
        tb_msg(&exp, 5, &info);
    }
    tb_double(&exp, 3, tb_now());
    tb_msg(&data, 2, &exp);
    tb_hparams_event(tb, "_hparams_/experiment", &data);
    free(exp.buf);
    free(info.buf);
    free(sub.buf);
    free(data.buf);
}

// SessionStartInfo{hparams map<string, Value{number_value}>, start_time_secs}.
static inline void tb_hparams_session_start(TbLog* tb, const char** names,
        const double* values, int n) {
    TbLog ssi = {0}, entry = {0}, value = {0}, data = {0};
    for (int i = 0; i < n; i++) {
        tb_double(&value, 2, values[i]);
        tb_bytes(&entry, 1, names[i], strlen(names[i]));
        tb_msg(&entry, 2, &value);
        tb_msg(&ssi, 1, &entry);
    }
    tb_double(&ssi, 5, tb_now());
    tb_msg(&data, 3, &ssi);
    tb_hparams_event(tb, "_hparams_/session_start_info", &data);
    free(ssi.buf);
    free(entry.buf);
    free(value.buf);
    free(data.buf);
}

#define TB_STATUS_SUCCESS 1
#define TB_STATUS_FAILURE 2

static inline void tb_hparams_session_end(TbLog* tb, int status) {
    TbLog sei = {0}, data = {0};
    tb_key(&sei, 1, 0);
    tb_varint(&sei, (uint64_t)status);
    tb_double(&sei, 2, tb_now());
    tb_msg(&data, 4, &sei);
    tb_hparams_event(tb, "_hparams_/session_end_info", &data);
    free(sei.buf);
    free(data.buf);
}

static inline void tb_close(TbLog* tb) {
    if (tb->fp) {
        fclose(tb->fp);
    }
    free(tb->buf);
    memset(tb, 0, sizeof(*tb));
}
