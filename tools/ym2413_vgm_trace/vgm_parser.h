#pragma once
#include <cstdint>
#include <vector>
#include <string>

struct VGMCommand {
    uint8_t  opcode;     // 生 opcode
    uint8_t  addr;       // 0x51: addr
    uint8_t  data;       // 0x51: data
    uint32_t wait;       // wait コマンドならサンプル数
    bool     is_write;   // YM2413 write なら true
};

struct VGMHeaderInfo {
    uint32_t version = 0;
    uint32_t data_offset_abs = 0;
    uint32_t total_samples = 0;
    uint32_t loop_offset_abs = 0;
    uint32_t loop_samples = 0;
    uint32_t ym2413_clock = 0;
};

class VGMParser {
public:
    bool load(const std::string& path, std::string& err, bool debug=false);
    const std::vector<VGMCommand>& commands() const { return m_cmds; }
    const VGMHeaderInfo& header() const { return m_hdr; }
private:
    std::vector<uint8_t> m_blob;
    std::vector<VGMCommand> m_cmds;
    VGMHeaderInfo m_hdr;
    bool parse(std::string& err, bool debug);
    void dump_hex_prefix(size_t count) const;
};