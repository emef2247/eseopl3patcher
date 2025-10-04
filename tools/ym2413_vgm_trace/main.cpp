#include <iostream>
#include <string>
#include <cstdint>
#include <cmath>
#include "vgm_parser.h"
#include "csv_writer.h"
#include "opll_probe.h"

static double compute_rms_db(double sum_sq, uint32_t n, double full_scale) {
    if (n == 0 || sum_sq <= 0.0) return -240.0;
    double rms = std::sqrt(sum_sq / (double)n);
    if (rms <= 0.0) return -240.0;
    double db = 20.0 * std::log10(rms / full_scale);
    if (db < -240.0) db = -240.0;
    return db;
}

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cerr << "Usage: ym2413_vgm_trace <input.vgm> <out.csv> [--debug]\n";
        return 1;
    }
    std::string in_vgm = argv[1];
    std::string out_csv = argv[2];
    bool debug = false;
    for (int i=3;i<argc;i++) {
        if (std::string(argv[i]) == "--debug") debug = true;
    }

    VGMParser parser;
    std::string err;
    if (!parser.load(in_vgm, err, debug)) {
        std::cerr << "Parse error: " << err << "\n";
        return 1;
    }
    if (parser.header().ym2413_clock == 0 && debug) {
        std::cerr << "[INFO] YM2413 clock is 0 in header (possibly stripped). Writes may still exist.\n";
    }

    CSVWriter csv;
    if (!csv.open(out_csv)) {
        std::cerr << "Cannot open csv: " << out_csv << "\n";
        return 1;
    }
    csv.header();

    // YM2413 clock: 0 の場合は典型値 3579545 を使ってよい
    uint32_t clk = parser.header().ym2413_clock ? parser.header().ym2413_clock : 3579545;
    uint32_t sample_rate = 44100; // 仮固定
    OpllProbe probe(clk, sample_rate);

    const auto& cmds = parser.commands();
    uint64_t t_samples = 0;
    int session_id = 0;
    int focus_ch = -1;

    auto ch_from_addr_reg2n = [](uint8_t addr)->int {
        if (addr >= 0x20 && addr <= 0x28) return addr - 0x20;
        return -1;
    };

    for (size_t i = 0; i < cmds.size(); ++i) {
        const auto& c = cmds[i];
        if (c.is_write && c.opcode == 0x51) {
            probe.write(c.addr, c.data);
            int ch = ch_from_addr_reg2n(c.addr);
            if (ch >= 0) {
                bool ko = (c.data & 0x10) != 0;
                if (ko) {
                    session_id++;
                    if (focus_ch < 0) focus_ch = ch;
                    csv.event_ko_on(session_id, ch, t_samples, c.data);
                } else {
                    csv.event_ko_off(session_id, ch, t_samples, c.data);
                }
            }
        }
        else if (c.wait > 0) {
            OpllSampleStats stats;
            probe.render(c.wait, stats);
            double mean_abs = (c.wait > 0) ? (stats.sum_abs / (double)c.wait) : 0.0;
            double fs = 32768.0;
            double rms_db = compute_rms_db(stats.sum_sq, c.wait, fs);
            int att_mod=-1, att_car=-1;
            double att_mod_db=-240.0, att_car_db=-240.0;
            csv.event_wait(session_id, focus_ch, t_samples, c.wait,
                           mean_abs, rms_db, stats.last_nonzero,
                           att_mod, att_car, att_mod_db, att_car_db);
            t_samples += c.wait;
        }
        else if (c.opcode == 0x66) {
            break;
        }
    }

    csv.close();

    if (debug) {
        std::cerr << "[STATS] Parsed commands=" << cmds.size()
                  << " last t_samples=" << t_samples
                  << " sessions=" << session_id << "\n";
    }
    if (session_id == 0) {
        std::cerr << "[WARN] No YM2413 KO_ON detected. Either the VGM has no 0x51 writes or parsing missed them.\n";
    }
    return 0;
}