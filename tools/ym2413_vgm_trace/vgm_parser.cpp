#include "vgm_parser.h"
#include <fstream>
#include <cstdio>
#include <cstring>

#pragma pack(push,1)
struct RawVGMHeader {
    char     ident[4];          // "Vgm "
    uint32_t eof_offset;        // file length - 4
    uint32_t version;           // BCD e.g. 0x00000170
    uint32_t sn76489_clock;
    uint32_t ym2413_clock;
    uint32_t gd3_offset;
    uint32_t total_samples;
    uint32_t loop_offset;
    uint32_t loop_samples;
    uint32_t rate;
    // 0x40 以降拡張(今回は最低限)
};
#pragma pack(pop)

static uint32_t read_u32_le(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1]<<8) | ((uint32_t)p[2]<<16) | ((uint32_t)p[3]<<24);
}

bool VGMParser::load(const std::string& path, std::string& err, bool debug) {
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs) { err = "open failed: " + path; return false; }
    m_blob.assign(std::istreambuf_iterator<char>(ifs), {});
    if (m_blob.size() < 0x40) { err = "file too small"; return false; }
    return parse(err, debug);
}

void VGMParser::dump_hex_prefix(size_t count) const {
    size_t lim = (count < m_blob.size()) ? count : m_blob.size();
    for (size_t i=0;i<lim;i+=16) {
        std::printf("[HEX]%06zx:", i);
        size_t line_end = (i+16<lim)? i+16:lim;
        for (size_t j=i;j<line_end;++j) std::printf(" %02X", m_blob[j]);
        std::printf("\n");
    }
}

bool VGMParser::parse(std::string& err, bool debug) {
    m_cmds.clear();
    const auto* hdr = reinterpret_cast<const RawVGMHeader*>(m_blob.data());
    if (std::memcmp(hdr->ident, "Vgm ", 4) != 0) {
        err = "not VGM";
        return false;
    }
    m_hdr.version       = hdr->version;
    m_hdr.total_samples = hdr->total_samples;
    m_hdr.loop_samples  = hdr->loop_samples;
    m_hdr.ym2413_clock  = hdr->ym2413_clock;

    // data offset (spec: at 0x34, relative to 0x34 if non-zero)
    uint32_t rel = read_u32_le(&m_blob[0x34]);
    m_hdr.data_offset_abs = (rel == 0) ? 0x40 : (0x34 + rel);

    // loop offset absolute
    if (hdr->loop_offset)
        m_hdr.loop_offset_abs = 0x1C + hdr->loop_offset; // loop_offset relative to 0x1C

    if (debug) {
        std::printf("[VGM] version=0x%08X ym2413_clk=%u total_samples=%u data_off=0x%X loop_off=0x%X\n",
            m_hdr.version, m_hdr.ym2413_clock, m_hdr.total_samples,
            m_hdr.data_offset_abs, m_hdr.loop_offset_abs);
        dump_hex_prefix(64);
    }

    if (m_hdr.data_offset_abs >= m_blob.size()) {
        err = "invalid data offset";
        return false;
    }

    size_t p = m_hdr.data_offset_abs;
    while (p < m_blob.size()) {
        uint8_t op = m_blob[p++];
        if (op == 0x66) { // end
            m_cmds.push_back({op,0,0,0,false});
            break;
        }
        else if (op == 0x51) { // YM2413 write: addr + data
            if (p + 2 > m_blob.size()) break;
            uint8_t addr = m_blob[p++];
            uint8_t data = m_blob[p++];
            m_cmds.push_back({op, addr, data, 0, true});
        }
        else if (op == 0x61) {
            if (p + 2 > m_blob.size()) break;
            uint16_t w = (uint16_t)(m_blob[p] | (m_blob[p+1] << 8));
            p += 2;
            m_cmds.push_back({op,0,0,(uint32_t)w,false});
        }
        else if (op == 0x62) {
            m_cmds.push_back({op,0,0,735,false});
        }
        else if (op == 0x63) {
            m_cmds.push_back({op,0,0,882,false});
        }
        else if (op >= 0x70 && op <= 0x7F) {
            uint32_t w = (op & 0x0F) + 1;
            m_cmds.push_back({op,0,0,w,false});
        }
        else if (op == 0x50) { // PSG write (skip 1 byte)
            if (p >= m_blob.size()) break;
            p++;
        }
        else if (op == 0x4F) { // GG stereo
            if (p >= m_blob.size()) break;
            p++;
        }
        else if (op == 0x67) {
            // Data block: expect 0x66 after
            if (p + 2 > m_blob.size()) break;
            uint8_t sig = m_blob[p++];  // should be 0x66
            uint8_t type = m_blob[p++];
            if (sig != 0x66) {
                if (debug) std::printf("[WARN] data block sig mismatch (%02X)\n", sig);
                // 再同期不可 → break
                break;
            }
            if (p + 4 > m_blob.size()) break;
            uint32_t len = read_u32_le(&m_blob[p]);
            p += 4;
            if (p + len > m_blob.size()) break;
            // skip data
            p += len;
        }
        else if (op == 0xE0) { // PCM seek
            if (p + 4 > m_blob.size()) break;
            p += 4;
        }
        else {
            if (debug) {
                std::printf("[WARN] unsupported opcode 0x%02X at 0x%zx (stop parse)\n", op, p-1);
            }
            // 安全のため break（欲しければ continue にして別途スキップロジック）
            break;
        }
    }
    return true;
}