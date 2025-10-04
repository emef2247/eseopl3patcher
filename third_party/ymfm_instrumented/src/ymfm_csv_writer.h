// BSD 3-Clause License
//
// Simple CSV writer utility for YMFM instrumentation
// Header-only implementation using FILE*

#ifndef YMFM_CSV_WRITER_H
#define YMFM_CSV_WRITER_H

#include <cstdio>
#include <cstdint>

namespace ymfm {

class CsvWriter
{
public:
    CsvWriter() : m_fp(nullptr) {}
    
    ~CsvWriter() {
        if (m_fp) {
            std::fclose(m_fp);
        }
    }
    
    bool open(const char* path) {
        if (m_fp) return false;
        m_fp = std::fopen(path, "w");
        return m_fp != nullptr;
    }
    
    void close() {
        if (m_fp) {
            std::fclose(m_fp);
            m_fp = nullptr;
        }
    }
    
    bool is_open() const { return m_fp != nullptr; }
    
    // Write a single string field
    void write_field(const char* str) {
        if (m_fp) std::fprintf(m_fp, "%s", str);
    }
    
    // Write an integer field
    void write_field(int value) {
        if (m_fp) std::fprintf(m_fp, "%d", value);
    }
    
    // Write an unsigned integer field
    void write_field(unsigned int value) {
        if (m_fp) std::fprintf(m_fp, "%u", value);
    }
    
    // Write a uint64_t field
    void write_field(uint64_t value) {
        if (m_fp) std::fprintf(m_fp, "%llu", (unsigned long long)value);
    }
    
    // Write a double field
    void write_field(double value) {
        if (m_fp) std::fprintf(m_fp, "%.16g", value);
    }
    
    // Write a hex byte field
    void write_hex(uint8_t value) {
        if (m_fp) std::fprintf(m_fp, "0x%02X", value);
    }
    
    // Write comma separator
    void write_comma() {
        if (m_fp) std::fprintf(m_fp, ",");
    }
    
    // Write newline
    void write_newline() {
        if (m_fp) std::fprintf(m_fp, "\n");
    }
    
    // Write empty field (nothing, just placeholder for comma)
    void write_empty() {
        // Just a placeholder, write nothing before the comma
    }
    
    // Flush the output
    void flush() {
        if (m_fp) std::fflush(m_fp);
    }
    
private:
    FILE* m_fp;
};

} // namespace ymfm

#endif // YMFM_CSV_WRITER_H
