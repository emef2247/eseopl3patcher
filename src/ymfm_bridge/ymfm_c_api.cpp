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
    uint64_t total_advanced = 0;
};

#else
// Stub when YMFM is disabled
struct ymfm_ctx {
    uint32_t clock_hz;
    uint32_t sample_rate;
    float last_mean_abs;
    uint64_t total_advanced;
};
#endif

extern "C" {

ymfm_ctx_t* ymfm_opll_create(uint32_t clock_hz, uint32_t sample_rate) {
    ymfm_ctx* ctx = new ymfm_ctx();
    ctx->clock_hz = clock_hz ? clock_hz : 3579545;
    ctx->sample_rate = sample_rate ? sample_rate : 44100;
    ctx->last_mean_abs = 0.0f;
    ctx->total_advanced = 0;

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

void ymfm_opll_write(ymfm_ctx_t* ctx, uint32_t addr, uint8_t data) {
    if (!ctx) return;
#if defined(USE_YMFM) && USE_YMFM
    ctx->chip->write(addr & 0xFF, data);
#else
    (void)addr; (void)data;
#endif
}

float ymfm_step_and_measure(ymfm_ctx_t* ctx, uint32_t n_samples) {
    if (!ctx || n_samples == 0) return 0.0f;

#if defined(USE_YMFM) && USE_YMFM
    using output_t = ymfm::opll_base::output_data; // ymfm_output<2>
    std::vector<output_t> frames(n_samples);
    ctx->chip->generate(frames.data(), n_samples);

    double sum_abs = 0.0;
    for (uint32_t i = 0; i < n_samples; ++i) {
        int32_t l = frames[i].data[0];
        int32_t r = frames[i].data[1];
        int32_t a = std::max(std::abs(l), std::abs(r));
        sum_abs += static_cast<double>(a);
    }
    double mean_abs = (sum_abs / (double)n_samples) / (double)(1 << 23);
    if (mean_abs < 0.0) mean_abs = 0.0;
    if (mean_abs > 1.0) mean_abs = 1.0;
    ctx->last_mean_abs = static_cast<float>(mean_abs);
#else
    ctx->last_mean_abs = 0.0f;
#endif

    ctx->total_advanced += n_samples;
    return ctx->last_mean_abs;
}

void ymfm_debug_print(const ymfm_ctx_t* ctx, const char* tag) {
    if (!ctx) return;
    printf("[YMFM] %s mean_abs=%.6f advanced=%llu samples (clk=%u, fs=%u)\n",
           tag ? tag : "-",
           (double)ctx->last_mean_abs,
           (unsigned long long)ctx->total_advanced,
           (unsigned)ctx->clock_hz, (unsigned)ctx->sample_rate);
}

} // extern "C"