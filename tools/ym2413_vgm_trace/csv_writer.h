#pragma once
#include <string>
#include <cstdio>
#include <vector>

class CSVWriter {
public:
    bool open(const std::string& path);
    void close();
    void header();
    void event_ko_on(int session, int ch, uint64_t t_samples, uint8_t reg2n);
    void event_ko_off(int session, int ch, uint64_t t_samples, uint8_t reg2n);
    void event_wait(int session, int focus_ch,
                    uint64_t t_samples, uint32_t wait_samples,
                    double mean_abs, double rms_db, uint32_t last_nz,
                    int att_mod, int att_car, double att_mod_db, double att_car_db);
private:
    FILE* fp_ = nullptr;
};