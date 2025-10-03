// clang-format off
#include <stdint.h>
#include <stdio.h>
#include <math.h>
#include <vector>
#include <memory>

extern "C" {
#include "ymfm_c_api.h"
}

// USE_YMFM が有効な時にYMFMを取り込む
#if defined(USE_YMFM) && USE_YMFM
  #include "../../third_party/ymfm/src/ymfm_opl.h"
  using ymfm_u32 = uint32_t;

  // YMFMはインタフェースをテンプレ化して使う方式。
  // 最小のインタフェースを用意（割り込み等は未使用のためダミー）。
  class OpllHost : public ymfm::ymfm_interface {
  public:
    // IRQ等はOPLLでは基本未使用。必要になれば拡張。
    virtual void ymfm_set_irq(int state) override { (void)state; }
    virtual void ymfm_set_timer(uint32_t, int) override {}
    virtual void ymfm_set_busy_end(uint32_t) override {}
  };

  struct ymfm_ctx {
    uint32_t clock_hz = 3579545;   // 例: OPLL default
    uint32_t sample_rate = 44100;
    OpllHost host;
    std::unique_ptr<ymfm::ym2413> chip;     // YM2413本体
    // 直近の測定値
    float last_mean_abs = 0.0f;
    uint64_t total_advanced = 0;
  };

#else
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
  // YM2413のクロック/サンプルレートを与えて生成
  ctx->chip = std::make_unique<ymfm::ym2413>(ctx->clock_hz, ctx->sample_rate, ctx->host);
  // 必要なら初期化
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
  // YM2413はアドレス/データポートを持つが、YMFMのwriteは直接レジスタ番地も受け付けるヘルパを備える。
  // ym2413::write(address, data) は (addr,data) 直書きAPI。
  ctx->chip->write(addr & 0xFF, data);
#else
  (void)addr; (void)data;
#endif
}

float ymfm_step_and_measure(ymfm_ctx_t* ctx, uint32_t n_samples) {
  if (!ctx || n_samples == 0) return 0.0f;

#if defined(USE_YMFM) && USE_YMFM
  // YMFMは1サンプルずつ16-bit相当を返す generate() を持つ。
  // ym2413::generate(pointer, count) でステレオ/モノの取り扱いは内部定義に従う。
  // ここでは左右同一のモノラル相当を集計（簡易観測）。
  std::vector<int32_t> mixbuf((size_t)n_samples * 2, 0); // stereo LR
  ctx->chip->generate(&mixbuf[0], (int)n_samples);

  double sum_abs = 0.0;
  // L/Rの絶対値の大きい方を代表として採用（簡易）
  for (uint32_t i = 0; i < n_samples; ++i) {
    int32_t l = mixbuf[i*2 + 0];
    int32_t r = mixbuf[i*2 + 1];
    int32_t a = (std::abs(l) > std::abs(r)) ? std::abs(l) : std::abs(r);
    sum_abs += (double)a;
  }
  // YMFMの出力スケールは実装依存。経験的に 24-bit相当で正規化して0..1目安に丸める。
  double mean_abs = (sum_abs / (double)n_samples) / (double)(1 << 23);
  if (mean_abs < 0.0) mean_abs = 0.0;
  if (mean_abs > 1.0) mean_abs = 1.0;
  ctx->last_mean_abs = (float)mean_abs;
#else
  // スタブ: YMFM無効時は0を返す
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