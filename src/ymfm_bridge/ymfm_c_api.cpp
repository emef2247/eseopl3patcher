// Minimal YMFM C++ bridge for OPLL (YM2413) analysis use (API-diff tolerant)
#include <stdint.h>
#include <stdio.h>
#include <math.h>
#include <vector>
#include <memory>
#include <algorithm>
#include <type_traits>
#include <utility>

extern "C" {
#include "ymfm_c_api.h"
}

#if defined(USE_YMFM) && USE_YMFM
  #include "../../third_party/ymfm/src/ymfm_opl.h"

// YMFM interface host (timers/irq/busy are no-op for analysis)
class OpllHost : public ymfm::ymfm_interface {
public:
    void ymfm_set_timer(uint32_t /*tnum*/, int32_t /*duration_in_clocks*/) override {}
    void ymfm_set_busy_end(uint32_t /*clocks*/) override {}
    void ymfm_update_irq(bool /*asserted*/) override {}
    uint8_t ymfm_external_read(ymfm::access_class /*type*/, uint32_t /*address*/) override { return 0; }
    void ymfm_external_write(ymfm::access_class /*type*/, uint32_t /*address*/, uint8_t /*data*/) override {}
};

// --------- API-diff tolerant helpers (C++17 SFINAE) ---------
template <class T>
struct has_set_unscaled_clock {
    template <class U>
    static auto test(int) -> decltype(std::declval<U&>().set_unscaled_clock(uint32_t{}), std::true_type{});
    template <class>
    static auto test(...) -> std::false_type;
    static constexpr bool value = decltype(test<T>(0))::value;
};

template <class T>
inline void try_set_unscaled_clock(T& chip, uint32_t clk) {
    if constexpr (has_set_unscaled_clock<T>::value) {
        chip.set_unscaled_clock(clk);
    } else {
        (void)chip; (void)clk;
    }
}

template <class T>
struct has_set_framerate {
    template <class U>
    static auto test(int) -> decltype(std::declval<U&>().set_framerate(uint32_t{}), std::true_type{});
    template <class>
    static auto test(...) -> std::false_type;
    static constexpr bool value = decltype(test<T>(0))::value;
};

template <class T>
inline void try_set_framerate(T& chip, uint32_t rate) {
    if constexpr (has_set_framerate<T>::value) {
        chip.set_framerate(rate);
    } else {
        (void)chip; (void)rate;
    }
}

struct ymfm_ctx {
    uint32_t clock_hz = 3579545;   // YM2413 default
    uint32_t sample_rate = 44100;
    OpllHost host;
    std::unique_ptr<ymfm::ym2413> chip; // YM2413 core
    float last_mean_abs = 0.0f;
    float last_rms_db = -120.0f;
    uint64_t total_advanced = 0;
    uint32_t last_nonzero = 0;
};

#else
// Stub when YMFM is disabled
struct ymfm_ctx {
    uint32_t clock_hz;
    uint32_t sample_rate;
    float last_mean_abs;
    float last_rms_db;
    uint64_t total_advanced;
    uint32_t last_nonzero;
};
#endif

extern "C" {

ymfm_ctx_t* ymfm_opll_create(uint32_t clock_hz, uint32_t sample_rate) {
    ymfm_ctx* ctx = new ymfm_ctx();
    ctx->clock_hz = clock_hz ? clock_hz : 3579545;
    ctx->sample_rate = sample_rate ? sample_rate : 44100;
    ctx->last_mean_abs = 0.0f;
    ctx->last_rms_db = -120.0f;
    ctx->total_advanced = 0;
    ctx->last_nonzero = 0;

#if defined(USE_YMFM) && USE_YMFM
    ctx->chip = std::make_unique<ymfm::ym2413>(ctx->host, nullptr);
    try_set_unscaled_clock(*ctx->chip, ctx->clock_hz);
    try_set_framerate(*ctx->chip, ctx->sample_rate);
    ctx->chip->reset();
#else
    (void)clock_hz; (void)sample_rate;
#endif
    return ctx;
}

void ymfm_destroy(ymfm_ctx_t* ctx) {
    if (!ctx) return;
#if defined(USE_YMFM) && USE_YMFM
    ctx->chip.reset();
#endif
    delete ctx;
}

void ymfm_opll_write(ymfm_ctx_t* ctx, uint32_t reg, uint8_t data) {
    if (!ctx) return;
#if defined(USE_YMFM) && USE_YMFM
    // YM2413 bus: write address to offset 0, then data to offset 1
    ctx->chip->write(0, (uint8_t)(reg & 0xFF)); // address latch
    ctx->chip->write(1, data);                  // data write
#else
    (void)reg; (void)data;
#endif
}

static inline void measure_common(ymfm_ctx* ctx, uint32_t n_samples, bool compute_db) {
#if defined(USE_YMFM) && USE_YMFM
    using output_t = ymfm::opll_base::output_data; // ymfm_output<2>
    std::vector<output_t> frames(n_samples);
    ctx->chip->generate(frames.data(), n_samples);

    double sum_abs = 0.0;
    double sum_sq  = 0.0;
    uint32_t nz = 0;
    for (uint32_t i = 0; i < n_samples; ++i) {
        int32_t l = frames[i].data[0];
        int32_t r = frames[i].data[1];
        int64_t ll = (int64_t)l;
        int64_t rr = (int64_t)r;
        int64_t a = std::max(ll >= 0 ? ll : -ll, rr >= 0 ? rr : -rr);
        if (a != 0) ++nz;
        sum_abs += (double)a;
        if (compute_db) {
            // Stereo RMS: (L^2 + R^2)/2 の平均 → ここでは縦続きで平均
            double lf = (double)l;
            double rf = (double)r;
            sum_sq += 0.5 * (lf*lf + rf*rf);
        }
    }

    // mean_abs: ざっくり正規化（~20-22bit想定）。控えめに2^20で割る。
    double mean_abs = (sum_abs / (double)n_samples) / (double)(1 << 20);
    if (mean_abs < 0.0) mean_abs = 0.0;
    if (mean_abs > 1.0) mean_abs = 1.0;
    ctx->last_mean_abs = (float)mean_abs;

    if (compute_db) {
        double rms = sqrt(sum_sq / (double)n_samples);           // 生PCMのRMS
        double norm = (double)(1 << 23);                         // ~24bitスケール
        double rms_norm = rms / norm;
        if (rms_norm < 1e-12) rms_norm = 1e-12;
        ctx->last_rms_db = (float)(20.0 * log10(rms_norm));      // dBFS
    }

    ctx->last_nonzero = nz;
    ctx->total_advanced += n_samples;
#else
    (void)n_samples; (void)compute_db;
    ctx->last_mean_abs = 0.0f;
    ctx->last_rms_db   = -120.0f;
    ctx->last_nonzero  = 0;
#endif
}

float ymfm_step_and_measure(ymfm_ctx_t* ctx, uint32_t n_samples) {
    if (!ctx) return 0.0f;
    // 追加: n_samples==0 は「前回の値を返す」だけ（前進しない）
    if (n_samples == 0) {
        return ctx->last_mean_abs;
    }
    measure_common(ctx, n_samples, false);
    return ctx->last_mean_abs;
}

float ymfm_step_and_measure_db(ymfm_ctx_t* ctx, uint32_t n_samples) {
    if (!ctx || n_samples == 0) return -120.0f;
    measure_common(ctx, n_samples, true);
    return ctx->last_rms_db;
}

uint32_t ymfm_get_last_nonzero(const ymfm_ctx_t* ctx) {
    return ctx ? ctx->last_nonzero : 0;
}

void ymfm_debug_print(const ymfm_ctx_t* ctx, const char* tag) {
    if (!ctx) return;
    printf("[YMFM] %s mean_abs=%.6f rms_db=%.2f nz=%u advanced=%llu samples (clk=%u, fs=%u)\n",
           tag ? tag : "-",
           (double)ctx->last_mean_abs,
           (double)ctx->last_rms_db,
           (unsigned)ctx->last_nonzero,
           (unsigned long long)ctx->total_advanced,
           (unsigned)ctx->clock_hz, (unsigned)ctx->sample_rate);
}

} // extern "C"