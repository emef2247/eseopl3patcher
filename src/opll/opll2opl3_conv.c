#include <stdio.h>
#include "../opl3/opl3_convert.h"
#include "opll2opl3_conv.h"
#include "../vgm/vgm_header.h"
#include "../vgm/vgm_helpers.h"
#include "../opll/ym2413_voice_rom.h"
#include "../opll/nukedopll_voice_rom.h"
#include <stdbool.h>
#include <string.h>

OPLL2OPL3_Scheduler g_scheduer;
#define YM2413_REGS_SIZE 0x40

// リズムパートの仮想チャンネルID定義（通常chと区別しても良い）
#define CH_BD  6   // Bass Drum
#define CH_SD  7   // Snare Drum / Tom
#define CH_CYM 8   // Cymbal / HiHat

#define SAMPLE_RATE 44100.0
#define MIN_GATE_MS 2         /* ensure at least ~2ms key-on */
#define MIN_GATE_SAMPLES ((uint32_t)((MIN_GATE_MS * SAMPLE_RATE) / 1000.0 + 0.5))
#define MAX_PENDING_MS 50     /* max time to wait for missing params */
#define MAX_PENDING_SAMPLES ((uint32_t)((MAX_PENDING_MS * SAMPLE_RATE) / 1000.0 + 0.5))

void opll2opl3_init_scheduler(OPLL2OPL3_Scheduler *s) {
    memset(s, 0, sizeof(*s));
    s->virtual_time = 0;
    s->emit_time = 0;

    for (int ch = 0; ch < OPLL_NUM_CHANNELS; ch++) {
        OPLL2OPL3_PendingChannel *p = &s->ch[ch];
        p->has_fnum_low = false ;   // 1n (0x10..0x18)
        p->has_fnum_high = false;  // 2n (0x20..0x28 includes key bit and block)
        p->has_tl        = false;         // 0x30..0x38
        p->has_voice     = false;      // 0x30..0x38 upper bits or 0x00..0x07 user patch
        p->has_key       = false;        // whether key bit was seen (1)
        p->is_pending    = false ;        // we delayed keyon because not all pieces arrived
        p->is_active     = false ;         // currently logically KeyOn (after flush active=true)
        p->is_pending_keyoff = false; // KeyOff waiting to be applied (handled in flush)

    /* stored param values (latest seen while pending) */
        p->fnum_low = 0;
        p->fnum_high = 0;   // lower 2 bits used
        p->fnum_comb = 0;  // assembled 10-bit fnum (maintain)
        p->block = 0;
        p->tl = 0;
        p->voice_id = 0;

    /* register  */
        p->valid_1n = 0;
        p->valid_2n = 0;
        p->valid_3n = 0;
        p->last_1n = 0;
        p->last_2n = 0;
        p->last_3n = 0;

    /* time stamp */
        p->keyon_time = 0;    // virtual time when KeyOn was first seen
        p->last_update_time = 0;   // last time param updated
        memset(p->last_emitted_reg_val, 0xFF, sizeof(p->last_emitted_reg_val)); // 未使用状態
    }
}

/* Convert OPLL fnum/block to OPL3 fnum/block (10-bit fnum, 3bit block)
   returns true if conversion within range */
bool convert_fnum_block_from_opll_to_opl3(
    double opll_clock, double opl3_clock,
    uint8_t opll_block, uint16_t opll_fnum,
    uint16_t *out_fnum, uint8_t *out_block)
{
    double freq = calc_opll_frequency(opll_clock, opll_block, opll_fnum);

    const double opl3_base = (opl3_clock / 72.0) / 1048576.0; // 2^20 = 1048576
    double best_err = 1e9;
    uint16_t best_f = 0;
    uint8_t best_b = 0;
    bool found = false;

    for (int b = 0; b < 8; ++b) {
        double denom = opl3_base * ldexp(1.0, b);
        double fnum_d = freq / denom;
        if (fnum_d < 0.0 || fnum_d > 1023.0) continue;
        uint16_t fnum_i = (uint16_t)(fnum_d + 0.5);
        if (fnum_i > 1023) continue;
        double freq_est = denom * (double)fnum_i;
        double err = fabs(freq_est - freq);
        if (err < best_err) {
            best_err = err;
            best_f = fnum_i;
            best_b = (uint8_t)b;
            found = true;
        }
    }
    if (!found) return false;
    *out_fnum = best_f;
    *out_block = best_b;
    return true;
}


void emit_opl3_reg_write(VGMBuffer *p_music_data,VGMContext *p_vgm_context, OPL3State *p_state, uint8_t addr, uint8_t val, const CommandOptions *p_opts, OPLL2OPL3_Scheduler *s, OPLL2OPL3_PendingChannel *p) 
{
    // 冗長抑止：最後に出した同アドレスと同じデータならスキップ
    uint8_t last = p->last_emitted_reg_val[addr];
    if (last == val) return;

    // OPL3 Register向けに書き込み
    duplicate_write_opl3(p_vgm_context,&p_vgm_context->status,p_state, addr, val, p_opts);

    p->last_emitted_reg_val[addr] = val;
    // レジスタ書き込み自体は時間を消費しない（waitで刻む）
}

void emit_wait(VGMBuffer *p_buf, VGMStatus *p_vstat, uint16_t samples, OPLL2OPL3_Scheduler *s) {
    if (samples == 0) return;
    vgm_wait_samples(p_buf, p_vstat,samples);
    s->emit_time += samples;
}

// FNUMレジスタ判定 (ピッチ下位8bit)
static inline bool is_opll_fnum_reg(uint8_t reg)
{
    return (reg >= 0x10 && reg <= 0x18);
}

// KeyOn判定
static inline bool is_opll_keyon_reg(uint8_t reg, uint8_t val)
{
    if (reg >= 0x20 && reg <= 0x28)
        return (val & 0x10) != 0;  // bit4=KeyOn
    return false;
}

// KeyOff判定
static inline bool is_opll_keyoff_reg(uint8_t reg, uint8_t val)
{
    if (reg >= 0x20 && reg <= 0x28)
        return (val & 0x10) == 0;  // bit4=0ならKeyOff
    return false;
}

// 音量 (Total Level) 判定
// OPLLでは 0x30〜0x38 に TL(5bit) + Inst(4bit) が格納
// TL更新時は KeyOn中にもボリューム変化がある
static inline bool is_opll_tl_reg(uint8_t reg)
{
    return (reg >= 0x30 && reg <= 0x38);
}

// 音色(Voice/Instrument)レジスタ判定
// 0x00〜0x07 : ユーザー音色パラメータ
// 0x30〜0x38 の上位ビットでもプリセット音色指定が入る
static inline bool is_opll_voice_reg(uint8_t reg)
{
    // 音色定義そのもの
    if (reg <= 0x07)
        return true;
    // チャンネル別の音色番号設定 (bit5〜bit7部分)
    if (reg >= 0x30 && reg <= 0x38)
        return true;
    return false;
}

/* Extract channel index given register and rhythm flag (simple melodic mapping) */
int opll_reg_to_channel(uint8_t reg, int is_rhythm_mode) {
    // --- Rhythm mode handling ---
    if (is_rhythm_mode) {
        if (reg == 0x36) return CH_BD;   // Bass Drum
        if (reg == 0x37) return CH_SD;   // Snare/Tom
        if (reg == 0x38) return CH_CYM;  // Cymbal/HiHat
    }

    // --- Standard melodic channels ---
    if ((reg >= 0x10 && reg <= 0x18)) {
        return reg - 0x10;  // FNUM low
    }
    if ((reg >= 0x20 && reg <= 0x28)) {
        return reg - 0x20;  // BLOCK + KeyOn
    }
    if ((reg <= 0x07) || (reg >= 0x30 && reg <= 0x38)) {
        return reg - 0x30;  // TL + voice
    }

    // Not a per-channel register
    return -1;
}

OPLL_ChannelInfo opll_reg_to_channel_ex(uint8_t reg, int rhythm_mode) {
    OPLL_ChannelInfo info = { .ch_index = -1, .type = OPLL_CH_TYPE_INVALID };

    if (reg >= 0x10 && reg <= 0x18) {
        info.ch_index = reg - 0x10;
        info.type = OPLL_CH_TYPE_MELODIC;
        return info;
    }
    if (reg >= 0x20 && reg <= 0x28) {
        info.ch_index = reg - 0x20;
        info.type = OPLL_CH_TYPE_MELODIC;
        return info;
    }
    if ((reg <= 0x07) || (reg >= 0x30 && reg <= 0x38)) {
        int ch = reg - 0x30;
        if (rhythm_mode && ch >= 6) {
            info.type = OPLL_CH_TYPE_RHYTHM;
        } else {
            info.type = OPLL_CH_TYPE_MELODIC;
        }
        info.ch_index = ch;
        return info;
    }

    return info; // invalid
}

void set_opll_operator_params_for_OPL(VGMBuffer *p_music_data,VGMContext *p_vgm_context, OPL3State *p_state, uint8_t addr, uint8_t val, const CommandOptions *p_opts,uint8_t voice_id, OPLL2OPL3_Scheduler *s, OPLL2OPL3_PendingChannel *p,OPL3VoiceParam *p_vp)
{
    if (!p_vp) return;
    memset(p_vp, 0, sizeof(*p_vp));

    /* ソース選択 */
    const uint8_t *src;
    if (voice_id == 0 && p_state->reg)          src = p_state->reg;
    else if (voice_id >= 1 && voice_id <= 15)   src = YM2413_VOICES[voice_id - 1];
    else if (voice_id >= 16 && voice_id <= 20)  src = YM2413_RHYTHM_VOICES[voice_id - 16];
    else                                        src = YM2413_VOICES[0];

    if (p_opts && p_opts->debug.verbose) {
        fprintf(stderr,
            "[YM2413->OPL3] inst=%d RAW:"
            " %02X %02X %02X %02X  %02X %02X %02X %02X\n",
            voice_id, src[0],src[1],src[2],src[3],src[4],src[5],src[6],src[7]);
    }

    /* --- Modulator (0..3) --- */
    uint8_t m_ar_raw = (src[2] >> 4) & 0x0F;
    uint8_t m_dr_raw =  src[2]       & 0x0F;

    p_vp->op[0].am   = (src[0] >> 7) & 1;
    p_vp->op[0].vib  = (src[0] >> 6) & 1;
    p_vp->op[0].egt  = (src[0] >> 5) & 1;
    p_vp->op[0].ksr  = (src[0] >> 4) & 1;
    p_vp->op[0].mult =  src[0]       & 0x0F;
    p_vp->op[0].ksl  = (src[1] >> 6) & 3;
    p_vp->op[0].tl   =  src[1]       & 0x3F; /* Mod TL は存在 */
    //p_vp->op[0].ar   = rate_map_pick(m_ar_raw);
    //p_vp->op[0].dr   = rate_map_pick(m_dr_raw);
    p_vp->op[0].sl   = (src[3] >> 4) & 0x0F;
    p_vp->op[0].rr   =  src[3]       & 0x0F;
    p_vp->op[0].ws   = 0;

    //p_vp->op[0].ar = enforce_min_attack(p_vp->op[0].ar, "Mod", voice_id, 0);

    /* --- Carrier (4..7) --- */
    uint8_t c_ar_raw = (src[6] >> 4) & 0x0F;
    uint8_t c_dr_raw =  src[6]       & 0x0F;

    p_vp->op[1].am   = (src[4] >> 7) & 1;
    p_vp->op[1].vib  = (src[4] >> 6) & 1;
    p_vp->op[1].egt  = (src[4] >> 5) & 1;
    p_vp->op[1].ksr  = (src[4] >> 4) & 1;
    p_vp->op[1].mult =  src[4] & 0x0F;
    p_vp->op[1].ksl  = (src[5] >> 6) & 3;
    p_vp->op[1].tl   = 0; /* キャリアには TL レジスタが無いので基礎 0 */
    //p_vp->op[1].ar   = rate_map_pick(c_ar_raw);
    //p_vp->op[1].dr   = rate_map_pick(c_dr_raw);
    p_vp->op[1].sl   = (src[7] >> 4) & 0x0F;
    p_vp->op[1].rr   =  src[7]       & 0x0F;
    p_vp->op[1].ws   = 0;

    // p_vp->op[1].ar = enforce_min_attack(p_vp->op[1].ar, "Car", voice_id, 1);

    /* Feedback: YM2413 は byte0 下位 3bit */
    uint8_t fb = src[0] & 0x07;
    p_vp->fb[0]  = fb;
    p_vp->cnt[0] = 0;
    p_vp->is_4op = 0;
    p_vp->voice_no = voice_id;
    p_vp->source_fmchip = FMCHIP_YM2413;

    if (p_opts && p_opts->debug.verbose) {
        fprintf(stderr,
            "[YM2413->OPL3] inst=%d MOD TL=%u AR=%u DR=%u SL=%u RR=%u | "
            "CAR TL(base)=%u AR=%u DR=%u SL=%u RR=%u FB=%u\n",
            voice_id,
            p_vp->op[0].tl, p_vp->op[0].ar, p_vp->op[0].dr, p_vp->op[0].sl, p_vp->op[0].rr,
            p_vp->op[1].tl, p_vp->op[1].ar, p_vp->op[1].dr, p_vp->op[1].sl, p_vp->op[1].rr,
            fb);
    }

    //emit_reg_write(s, 0x20 + ch, voice->mod.AM_VIB_EG_KSR_MULT, p);
    //emit_reg_write(s, 0x40 + ch, voice->mod.KSL_TL, p);
    //emit_reg_write(s, 0x60 + ch, voice->mod.AR_DR, p);
    //emit_reg_write(s, 0x80 + ch, voice->mod.SL_RR, p);

    //emit_reg_write(s, 0x20 + ch + 3, voice->car.AM_VIB_EG_KSR_MULT, p);
    //emit_reg_write(s, 0x40 + ch + 3, voice->car.KSL_TL, p);
    //emit_reg_write(s, 0x60 + ch + 3, voice->car.AR_DR, p);
    //emit_reg_write(s, 0x80 + ch + 3, voice->car.SL_RR, p);
}


void flush_channel_for_OPL(VGMBuffer *p_music_data,VGMContext *p_vgm_context, OPL3State *p_state, uint8_t addr, uint8_t val, const CommandOptions *p_opts, OPLL2OPL3_Scheduler *s, int ch)
{
    OPLL2OPL3_PendingChannel *p = &s->ch[ch];

    // nothing to do
    if (!p->is_pending && !p->has_key && !p->is_pending_keyoff) return;

    // 1) Align virtual/emit: if virtual_time progressed, emit that wait first
    int64_t diff = (int64_t)s->virtual_time - (int64_t)s->emit_time;
    if (diff > 0) {
        // emit prior accumulated waits so that emitted reg writes occur at correct time
        emit_wait(&p_vgm_context->buffer, &p_vgm_context->status, (uint32_t)diff, s);
    }

    // 2) If a KeyOff is pending, ensure minimum gate length (MIN_GATE_SAMPLES)
    if (p->is_pending_keyoff) {
        sample_t gate_len = (p->keyon_time <= s->virtual_time) ? (s->virtual_time - p->keyon_time) : 0;
        
        if (gate_len < OPLL_MIN_GATE_SAMPLES) {
            uint32_t add_wait = (uint32_t)(OPLL_MIN_GATE_SAMPLES - gate_len);
            emit_wait(&p_vgm_context->buffer, &p_vgm_context->status, add_wait, s);
        }
        /* Emit KeyOff: for OPL3, clear key bit by writing B0+ch with bit5=0;
           But also, depending on design, you might emit FNUM high with key bit 0 */
        /* Here we simply emit B reg with 0 key bit (keep fnum/block) */
        uint8_t b_reg = 0xB0 + ch;
        uint8_t b_val = (uint8_t)((p->fnum_comb >> 8) & 0x03) | ((p->block & 0x07) << 2);
        /* ensure key bit 0 */
        emit_opl3_reg_write(p_music_data, p_vgm_context, p_state, b_reg, b_val, p_opts, s, p);

        // KeyOff processed: clear flags
        p->is_pending_keyoff = false;
        p->is_active = false;
        /* clear pending flags so future KeyOn uses fresh state */
        p->has_fnum_low = p->has_fnum_high = p->has_tl = p->has_voice = p->has_key = false;
        return;
    }

    // 3) If this is a pending KeyOn (we held it waiting), ensure we have required params
    if (p->is_pending) {
        bool ready = (p->has_fnum_low && p->has_fnum_high && p->has_tl && p->has_voice && p->has_key);
        if (!ready) {
            //* If too long waited, force flush anyway (safety) */
            sample_t waited = s->virtual_time - p->keyon_time;
            if (waited >= MAX_PENDING_SAMPLES) {
                ready = true; /* force */
            } else {
                /* still not ready: don't flush yet */
                return;
            }
        }

        /* at this point, do the full emit: operator params (voice), FNUM low, TL, FNUM high+key */
        /* For demo we skip operator params emission; user should call emit_operator_params_* here */
        /* 3a) emit operator params (omitted: call your mapping conversion prior to flush) */

        /* 3b) convert assembled opll fnum/block to target opl3 fnum/block */
        uint16_t dst_fnum; uint8_t dst_block;
        if (!convert_fnum_block_from_opll_to_opl3(p_vgm_context->source_fm_clock, p_vgm_context->target_fm_clock, p->block, p->fnum_comb, &dst_fnum, &dst_block)) {
            /* out of range => clamp to nearest (simple handling) */
            dst_fnum = p->fnum_comb & 0x3FF;
            dst_block = p->block & 0x07;
        }
        /* update internal to OPL3 values */
        p->fnum_comb = dst_fnum;
        p->block = dst_block;

        /* 3c) emit A reg (FNUM low) */
        emit_opl3_reg_write(p_music_data, p_vgm_context, p_state, (uint8_t)(0xA0 + ch), (uint8_t)(dst_fnum & 0xFF), p_opts, s, p);

        /* 3d) emit TL (carrier-centered) - simplified: write channel's TL reg (0x40+op) */
        /* In actual OPL3 you must write TL per operator; here we demonstrate by logging TL */
        emit_opl3_reg_write(p_music_data, p_vgm_context, p_state, (uint8_t)(0x40 + ch), p->tl & 0x3F, p_opts, s, p);

        /* 3e) emit B reg (FNUM high + block + keybit) */
        uint8_t b_val = (uint8_t)(((dst_fnum >> 8) & 0x03) | ((dst_block & 0x07) << 2) | 0x20); // set keyon bit
        emit_opl3_reg_write(p_music_data, p_vgm_context, p_state, (uint8_t)(0xB0 + ch), b_val, p_opts, s, p);
        OPL3VoiceParam vp_tmp;

        /* finalize */
        p->is_pending = false;
        p->is_active = true;
        /* we keep has_* flags as true representing current state */
    }
}

static inline bool is_keyon_bit_set(uint8_t val) { return (val & 0x10) != 0; }

void handle_opll_write(VGMBuffer *p_music_data,VGMContext *p_vgm_context, OPL3State *p_state, uint8_t reg, uint8_t val, const CommandOptions *p_opts, OPLL2OPL3_Scheduler *s)
{
        
    bool is_rhythm_mode = false;
    int ch = opll_reg_to_channel(reg, is_rhythm_mode);
    if (ch < 0 || ch >= OPLL_NUM_CHANNELS) {
        /* non-channel registers (or rhythm bits) ignored in this demo */
        return;
    }
    OPLL2OPL3_PendingChannel *p = &s->ch[ch];
    /* update timestamp */
    p->last_update_time = s->virtual_time;

    if ((reg >= 0x10 && reg <= 0x18)) {
        p->fnum_low = val;
        /* assemble lower bits; keep high part intact */
        p->fnum_comb = (uint16_t)(((p->fnum_comb & 0x300) | val) & 0x3FF);
        p->has_fnum_low = true;
        /* if we were pending and now have more, maybe flush */
        if (p->is_pending) flush_channel_for_OPL(p_music_data,p_vgm_context, p_state, reg, val, p_opts, s, ch);
        return;
    }
    if ((reg >= 0x20 && reg <= 0x28)) {
        /* high bits: fnum high 2 bits in low bits of val, block in bits 2..4? (per OPLL layout) */
        uint16_t fhi = (uint16_t)(val & 0x03);
        uint8_t block = (uint8_t)((val >> 2) & 0x07);
        bool keyon = is_keyon_bit_set(val);

        p->fnum_high = (uint8_t)fhi;
        p->fnum_comb = (uint16_t)((fhi << 8) | (p->fnum_comb & 0xFF));
        p->block = block;
        p->has_fnum_high = true;

        if (keyon) {
            p->has_key = true;
            /* if tl/voice not yet supplied, mark pending */
            if (!(p->has_tl && p->has_voice && p->has_fnum_low)) {
                p->is_pending = true;
                p->keyon_time = s->virtual_time;
                /* still attempt flush: flush_channel will check readiness and MAX_PENDING */
                flush_channel_for_OPL(p_music_data,p_vgm_context, p_state, reg, val, p_opts, s, ch);

            } else {
                /* all parts present: flush immediately */
                p->is_pending = true; /* set pending to let flush_channel do unified emit */
                flush_channel_for_OPL(p_music_data,p_vgm_context, p_state, reg, val, p_opts, s, ch);
            }
        } else {
            /* KeyOff: mark pending_keyoff so flush can handle min gate */
            if (p->is_active) {
                p->is_pending_keyoff = true;
                flush_channel_for_OPL(p_music_data,p_vgm_context, p_state, reg, val, p_opts, s, ch);
            } else {
                /* if not active, just clear flags */
                p->has_key = false;
                p->is_pending = false;
            }
        }
        return;
    }
    if ((reg >= 0x30 && reg <= 0x38)) {
        p->tl = val & 0x3F;
        p->has_tl = true;
        if (p->is_pending) flush_channel_for_OPL(p_music_data,p_vgm_context, p_state, reg, val, p_opts, s, ch);
        return;
    }
    if ((reg <= 0x07) || (reg >= 0x30 && reg <= 0x38)) {
        /* for demo, take voice as upper nibble if from ch reg, or user-patch area handling omitted */
        p->voice_id = (reg >= 0x30) ? (val >> 4) : val; 
        p->has_voice = true;
        if (p->is_pending) flush_channel_for_OPL(p_music_data,p_vgm_context, p_state, reg, val, p_opts, s, ch);
        return;
    }
}

void schedule_wait(VGMContext *p_vgm_context, uint32_t wait_samples, const CommandOptions *p_opts, OPLL2OPL3_Scheduler *s) {
    // まず virtual_time 側を進める（呼び出し元で既に virtual_time を増やしている場合は不要）
    s->virtual_time += wait_samples;

    // 現在の差分（virtual - emit）
    int64_t diff = (int64_t)s->virtual_time - (int64_t)s->emit_time;

    // adjusted_wait = incoming_wait - (virtual - emit) の差分吸収ロジック
    // ただしここでは読み込み時に virtual_time 増加済みのため、
    // 実際に出す wait は max(0, diff)
    uint32_t adjusted_wait = (diff > 0) ? (uint32_t)diff : 0u;
    if (adjusted_wait > 0) {
        emit_wait(&p_vgm_context->buffer, &p_vgm_context->status, wait_samples, s);
    }
    // emit_time 更新は emit_wait 内で行われる
}


/**
 * Main register write entrypoint for OPLL emulation.
 */
int opll2opl_command_handler (
    VGMBuffer *p_music_data,
    VGMContext *p_vgm_context,
    OPL3State *p_state,
    uint8_t reg, uint8_t val, uint16_t wait_samples,
    const CommandOptions *p_opts) {
    
    if (p_vgm_context->cmd_type == VGMCommandType_RegWrite) {
        handle_opll_write(p_music_data, p_vgm_context, p_state, reg, val, p_opts, &g_scheduer);
    } else if (p_vgm_context->cmd_type == VGMCommandType_Wait) {
        schedule_wait(p_vgm_context, wait_samples, p_opts, &g_scheduer);
    } else {
        // Should not be occured here
    }

}