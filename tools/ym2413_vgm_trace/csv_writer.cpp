#include "csv_writer.h"
#include <cmath>

bool CSVWriter::open(const std::string& path) {
    fp_ = std::fopen(path.c_str(), "w");
    return fp_ != nullptr;
}
void CSVWriter::close() { if (fp_) std::fclose(fp_), fp_ = nullptr; }
void CSVWriter::header() {
    if (!fp_) return;
    std::fprintf(fp_,
        "session_id,ch,t_samples,wait_samples,mean_abs,rms_db,nz,"
        "att_mod,att_mod_db,att_car,att_car_db,event,reg2n_hex\n");
}
void CSVWriter::event_ko_on(int session, int ch, uint64_t t_samples, uint8_t reg2n) {
    if (!fp_) return;
    std::fprintf(fp_,
        "%d,%d,%llu,0,0.000000,-240.00,0,-1,-240.00,-1,-240.00,KO_ON,%02X\n",
        session, ch, (unsigned long long)t_samples, reg2n);
}
void CSVWriter::event_ko_off(int session, int ch, uint64_t t_samples, uint8_t reg2n) {
    if (!fp_) return;
    std::fprintf(fp_,
        "%d,%d,%llu,0,0.000000,-240.00,0,-1,-240.00,-1,-240.00,KO_OFF,%02X\n",
        session, ch, (unsigned long long)t_samples, reg2n);
}
void CSVWriter::event_wait(int session, int focus_ch,
                           uint64_t t_samples, uint32_t wait_samples,
                           double mean_abs, double rms_db, uint32_t nz,
                           int att_mod, int att_car, double att_mod_db, double att_car_db)
{
    if (!fp_) return;
    std::fprintf(fp_,
        "%d,%d,%llu,%u,%.6f,%.2f,%u,%d,%.2f,%d,%.2f,WAIT,\n",
        session, focus_ch, (unsigned long long)t_samples,
        wait_samples, mean_abs, rms_db, nz,
        att_mod, att_mod_db, att_car, att_car_db);
}