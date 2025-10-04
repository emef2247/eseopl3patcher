# ymfm bridge (OPLL trace integration)

目的:
- OPLL (YM2413) の EG/KeyOn/KeyOff 状態を C API 経由で eseopl3patcher へ露出
- 音量測定 (mean_abs / rms_db / last_nonzero) のタイミングを統一

今後追加予定 API:
- ymfm_opll_create(clock, sample_rate)
- ymfm_opll_destroy()
- ymfm_opll_write(addr, data)          // 内部で key edge コールバック
- ymfm_step_and_measure(wait_samples, out_rms_db, out_mean_abs, out_last_nonzero)
- ymfm_get_op_env_phase(ch, op)
- ymfm_get_op_env_att(ch, op)

非改造ルール:
- ymfm_upstream/ 配下のファイルに直接手を入れない
- instrumentation は bridge 内の薄いラッパと friend アクセス（必要なら）だけで実現