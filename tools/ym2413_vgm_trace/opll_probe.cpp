#include "opll_probe.h"

OpllProbe::OpllProbe(uint32_t clock, uint32_t sample_rate)
: clock_(clock), sample_rate_(sample_rate)
{
    // 第二引数: instrument_data=nullptr -> デフォルトテーブル
    dev_ = new ymfm::ym2413(iface_, nullptr);
    dev_->reset();
    (void)clock_;
    (void)sample_rate_;
}

OpllProbe::~OpllProbe() {
    delete dev_;
    dev_ = nullptr;
}

void OpllProbe::write(uint8_t addr, uint8_t data) {
    if (!dev_) return;
    // offset 0 = address, offset 1 = data
    dev_->write(0, addr);
    dev_->write(1, data);
}

void OpllProbe::render(uint32_t samples, OpllSampleStats& out) {
    out = OpllSampleStats{};
    if (!dev_ || samples == 0) return;

    ymfm::ym2413::output_data frame;
    for (uint32_t i = 0; i < samples; ++i) {
        dev_->generate(&frame, 1);  // 1サンプル
        // YM2413出力は2ch (内部平均変換後) frame.data[0], frame.data[1]
        int32_t s = frame.data[0];
        if (s != 0) out.last_nonzero = i + 1;
        out.sum_abs += std::abs((double)s);
        out.sum_sq  += (double)s * (double)s;
    }
}