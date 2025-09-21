#include "debug_opts.h"
#include <string.h>
#include <stdio.h>

/* main.c などで g_dbg を定義している前提。
   ここで定義していない場合は:
     DebugOpts g_dbg = {0};
   を入れてください（多重定義注意）。 */

void apply_debug_overrides(OPL3VoiceParam *vp) {
    if (!vp) return;

    if (g_dbg.fast_attack) {
        for (int i = 0; i < 2; ++i) {
            vp->op[i].ar = 15;
            if (vp->op[i].dr < 4) vp->op[i].dr = 4;
            if (vp->op[i].rr < 2) vp->op[i].rr = 2;
        }
    }
    if (g_dbg.test_tone) {
        /* テストトーン：モジュレータをミュート・加算アルゴリズム */
        vp->cnt[0] = 1;
        vp->op[0].tl = 63;
    }
    if (g_dbg.no_post_keyon_tl) {
        /* 必要に応じて KeyOn 直後の TL 変更を抑制するロジックを追加 */
    }
    /* strip_non_opl, single_port は OPL3 出力段階で判定する想定 */
}