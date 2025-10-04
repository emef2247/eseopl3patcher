#pragma once
#include <cstdint>
#include <cmath>

#include "../../third_party/ymfm_upstream/src/ymfm.h"
#include "../../third_party/ymfm_upstream/src/ymfm_opl.h"

struct OpllSampleStats {
    double   sum_abs      = 0.0;
    double   sum_sq       = 0.0;
    uint32_t last_nonzero = 0;
};

class OpllProbe {
public:
    OpllProbe(uint32_t clock, uint32_t sample_rate);
    ~OpllProbe();

    void write(uint8_t addr, uint8_t data);
    void render(uint32_t samples, OpllSampleStats& out);

    // Envelope placeholders
    int    get_att_mod_raw(int) const { return -1; }
    int    get_att_car_raw(int) const { return -1; }
    double att_raw_to_db(int raw) const {
        if (raw < 0) return -240.0;
        return -(double)raw * (96.0 / 1023.0);
    }

private:
    class DummyInterface : public ymfm::ymfm_interface {
    public:
        DummyInterface() = default;
        // busy制御だけ簡易実装
        void ymfm_set_busy_end(uint32_t clocks) override {
            // 適当: 後続の ymfm_is_busy() 呼び出しでカウントダウン
            m_busy = clocks;
        }
        bool ymfm_is_busy() override {
            if (m_busy > 0) --m_busy;
            return m_busy != 0;
        }
        // 他はデフォルト実装（空）で十分
    private:
        uint32_t m_busy = 0;
    };

    DummyInterface  iface_;
    ymfm::ym2413*   dev_ = nullptr;
    uint32_t        clock_ = 0;
    uint32_t        sample_rate_ = 0;
};