#!/usr/bin/env python3
import argparse
from pathlib import Path
import pandas as pd
import numpy as np
import os

def pick_time_column(df: pd.DataFrame) -> str:
    if 'time_s' in df.columns:
        return 'time_s'
    if 'time' in df.columns:
        return 'time'
    # フォールバック
    return df.columns[0]

def load_csv_with_hash_header(path: Path) -> pd.DataFrame:
    """
    # をコメント扱いせずに #type 列を保持するフォールバック付きローダ
    """
    # まずは従来互換（コメント行スキップ）
    df = pd.read_csv(path, comment='#')
    if '#type' in df.columns:
        return df
    # フォールバック: コメント機構なしで再読込（ヘッダの #type を壊さない）
    df2 = pd.read_csv(path)
    return df2

def nearest_prev_event_time(df: pd.DataFrame, tcol: str, t_on: float):
    # df は単一チャネル・単一タイプ（A0 or C0）想定、時刻昇順
    vals = df[tcol].values
    idx = np.searchsorted(vals, t_on, side='right') - 1
    if idx >= 0:
        row = df.iloc[idx]
        return row[tcol], row
    return None, None

def nearest_next_event_time(df: pd.DataFrame, tcol: str, t_on: float):
    vals = df[tcol].values
    idx = np.searchsorted(vals, t_on, side='left')
    if idx < len(df):
        row = df.iloc[idx]
        return row[tcol], row
    return None, None

def extract_channel(df_fhbk: pd.DataFrame, df_fl: pd.DataFrame, df_iv: pd.DataFrame, tcol: str, window: float):
    # fHBK の立下り/立上がりで Gate 区間を抽出
    out_rows = []
    fh = df_fhbk.sort_values(tcol).reset_index(drop=True)
    prev_ko = 0
    on_events = []  # (index_in_fh, t_on)
    off_events = [] # (index_in_fh, t_off)
    for i, row in fh.iterrows():
        ko_val = row.get('ko', np.nan)
        ko = int(ko_val) if pd.notna(ko_val) else 0
        if prev_ko != ko:
            if ko == 1:
                on_events.append((i, float(row[tcol])))
            else:
                off_events.append((i, float(row[tcol])))
        prev_ko = ko

    off_times = [t for _, t in off_events]
    on_times = [t for _, t in on_events]

    df_fl_sorted = df_fl.sort_values(tcol).reset_index(drop=True)
    df_iv_sorted = df_iv.sort_values(tcol).reset_index(drop=True)

    for note_idx, (i_on, t_on) in enumerate(on_events):
        on_row = fh.iloc[i_on]

        # 次の Off / 次の On
        t_off = None
        close_reason = 'none'
        idx_off = np.searchsorted(off_times, t_on, side='right')
        if idx_off < len(off_times):
            t_off = off_times[idx_off]
            close_reason = 'ko_off'

        t_next_on = None
        idx_next_on = np.searchsorted(on_times, t_on, side='right')
        if idx_next_on < len(on_times):
            t_next_on = on_times[idx_next_on]

        dur_s = (t_off - t_on) if t_off is not None else np.nan
        interval_to_next_on_s = (t_next_on - t_on) if t_next_on is not None else np.nan
        dur_to_next_on_ratio = (dur_s / interval_to_next_on_s) if (np.isfinite(dur_s) and np.isfinite(interval_to_next_on_s) and interval_to_next_on_s > 0) else np.nan

        blk_on = on_row.get('blk', np.nan)
        fnum_on = on_row.get('fnum', np.nan)
        fnumL_on = on_row.get('fnumL', np.nan)

        # A0（fL）前後
        t_prev_a0, row_prev_a0 = nearest_prev_event_time(df_fl_sorted, tcol, t_on)
        t_next_a0, row_next_a0 = nearest_next_event_time(df_fl_sorted, tcol, t_on)
        dt_a0_before_on_s = (t_on - t_prev_a0) if t_prev_a0 is not None else np.nan
        dt_a0_after_on_s = (t_next_a0 - t_on) if t_next_a0 is not None else np.nan
        a0_before_on_in_window = (dt_a0_before_on_s <= window) if np.isfinite(dt_a0_before_on_s) else False
        a0_after_on_in_window = (dt_a0_after_on_s <= window) if np.isfinite(dt_a0_after_on_s) else False
        fnumL_before = row_prev_a0.get('fnumL', np.nan) if row_prev_a0 is not None else np.nan
        fnumL_after = row_next_a0.get('fnumL', np.nan) if row_next_a0 is not None else np.nan

        # C0（iv）前後
        t_prev_iv, row_prev_iv = nearest_prev_event_time(df_iv_sorted, tcol, t_on)
        t_next_iv, row_next_iv = nearest_next_event_time(df_iv_sorted, tcol, t_on)
        dt_iv_before_on_s = (t_on - t_prev_iv) if t_prev_iv is not None else np.nan
        dt_iv_after_on_s = (t_next_iv - t_on) if t_next_iv is not None else np.nan
        iv_before_on_in_window = (dt_iv_before_on_s <= window) if np.isfinite(dt_iv_before_on_s) else False
        iv_after_on_in_window = (dt_iv_after_on_s <= window) if np.isfinite(dt_iv_after_on_s) else False
        inst_before = row_prev_iv.get('inst', np.nan) if row_prev_iv is not None else np.nan
        vol_before = row_prev_iv.get('vol', np.nan) if row_prev_iv is not None else np.nan
        inst_after = row_next_iv.get('inst', np.nan) if row_next_iv is not None else np.nan
        vol_after = row_next_iv.get('vol', np.nan) if row_next_iv is not None else np.nan

        out_rows.append({
            'ch': int(on_row.get('ch', -1)),
            'note_index': note_idx,
            't_on_s': t_on,
            't_off_s': t_off,
            'dur_s': dur_s,
            't_next_on_s': t_next_on,
            'interval_to_next_on_s': interval_to_next_on_s,
            'dur_to_next_on_ratio': dur_to_next_on_ratio,
            'close_reason': close_reason,
            'blk_on': blk_on,
            'fnum_on': fnum_on,
            'fnumL_on': fnumL_on,
            'dt_a0_before_on_s': dt_a0_before_on_s,
            'dt_a0_after_on_s': dt_a0_after_on_s,
            'a0_before_on_in_window': a0_before_on_in_window,
            'a0_after_on_in_window': a0_after_on_in_window,
            'fnumL_before_on': fnumL_before,
            'fnumL_after_on': fnumL_after,
            'dt_iv_before_on_s': dt_iv_before_on_s,
            'dt_iv_after_on_s': dt_iv_after_on_s,
            'iv_before_on_in_window': iv_before_on_in_window,
            'iv_after_on_in_window': iv_after_on_in_window,
            'inst_before_on': inst_before,
            'vol_before_on': vol_before,
            'inst_after_on': inst_after,
            'vol_after_on': vol_after,
        })
    return out_rows

def main():
    ap = argparse.ArgumentParser(description='YM2413 duration extractor: segment by fHBK (B0) KO on/off and inspect A0/C0 timing.')
    ap.add_argument('--input', required=True, help='Path to *_log.opll.csv or typed YM2413 timeline CSV')
    ap.add_argument('--output', default=None, help='Output CSV path or directory')
    ap.add_argument('--channel', type=int, default=None, help='If specified, only process this channel number')
    ap.add_argument('--window', type=float, default=0.017, help='Window [sec] to judge A0/C0 updates as "near" the KO-on (default ~1 frame at 60Hz)')
    args = ap.parse_args()

    in_path = Path(args.input)

    # 出力先（ディレクトリ対応）
    if args.output:
        out_candidate = Path(args.output)
        if out_candidate.is_dir() or str(args.output).endswith(os.sep):
            out_path = out_candidate / (in_path.stem.replace('_log.opll', '').replace('_timeline_YM2413', '') + '_durations.csv')
        else:
            # 親がディレクトリだが未作成のケースもあるので mkdir
            out_candidate.parent.mkdir(parents=True, exist_ok=True)
            out_path = out_candidate
    else:
        out_path = in_path.with_name(in_path.stem.replace('_log.opll', '').replace('_timeline_YM2413', '') + '_durations.csv')

    # CSV 読み込み（#type ヘッダを壊さないように）
    df = load_csv_with_hash_header(in_path)

    want_types = {'fHBK', 'fL', 'iv'}
    if '#type' not in df.columns:
        raise SystemExit('CSV must have a "#type" column (fHBK/fL/iv).')
    if 'ch' not in df.columns:
        raise SystemExit('CSV must have a "ch" column.')
    tcol = pick_time_column(df)

    # 型整形
    for col in ['ch', 'ko', 'blk', 'fnum', 'fnumL', 'inst', 'vol']:
        if col in df.columns:
            df[col] = pd.to_numeric(df[col], errors='coerce')

    df = df[df['#type'].isin(want_types)].copy()

    rows = []
    channels = sorted(df['ch'].dropna().astype(int).unique())
    if args.channel is not None:
        channels = [c for c in channels if c == args.channel]

    for ch in channels:
        df_ch = df[df['ch'] == ch]
        df_fhbk = df_ch[df_ch['#type'] == 'fHBK']
        df_fl = df_ch[df_ch['#type'] == 'fL']
        df_iv = df_ch[df_ch['#type'] == 'iv']
        if df_fhbk.empty:
            continue
        rows.extend(extract_channel(df_fhbk, df_fl, df_iv, tcol, args.window))

    if not rows:
        print('No durations extracted (no fHBK KO edges found).')
        return

    out_df = pd.DataFrame(rows)
    out_df.sort_values(['ch', 't_on_s', 'note_index'], inplace=True)
    out_path.parent.mkdir(parents=True, exist_ok=True)
    out_df.to_csv(out_path, index=False)
    print(f'Wrote: {out_path}  ({len(out_df)} rows)')

if __name__ == '__main__':
    main()