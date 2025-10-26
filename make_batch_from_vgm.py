import os
import glob
import sys

def main(input_dir):
    # Find all files matching *OPLL.vgm in input_dir
    input_files = sorted(glob.glob(os.path.join(input_dir, "*OPLL.vgm")))
    if not input_files:
        print(f"No OPLL.vgm files found in {input_dir}")
        return

    # 出力先を input_dir 配下に
    batch_filename = os.path.join(input_dir, "eseopl3patcher_batch.bat")

    # Output subdirectory names and suffixes
    outputs = [
        ("YM2413",    "YVS.vgm",     "-preset_source YMVOICE -preset YM2413"),
        ("YM2413",    "YVM.vgm",     "-preset_source YMVOICE -preset YM2413 -k"),
		("YM2413",    "YFS.vgm",     "-preset_source YMFM -preset YM2413"),
		("YM2413",    "YFM.vgm",     "-preset_source YMFM -preset YM2413 -k"),
        ("YM2413",    "EXPS.vgm",    "-preset_source EXPERIMENT -preset YM2413"),
		("YM2413",    "EXPM.vgm",    "-preset_source EXPERIMENT -preset YM2413 -k"),
        ("YM2423",    "YFS.vgm",     "-preset_source YMFM -preset YM2423"),
        ("YM2423",    "YFM.vgm",     "-preset_source YMFM -preset YM2423 -k"),
        ("YM2423",    "EXPS.vgm",    "-preset_source EXPERIMENT -preset YM2423"),
        ("YM2423",    "EXPM.vgm",    "-preset_source EXPERIMENT -preset YM2423 -k"),
        ("YMF281B",   "YVS.vgm",     "-preset_source YMVOICE -preset YMF281B"),
		("YMF281B",   "YVM.vgm",     "-preset_source YMVOICE -preset YMF281B -k"),
        ("YMF281B",   "YFS.vgm",     "-preset_source YMFM -preset YMF281B"),
        ("YMF281B",   "YFM.vgm",     "-preset_source YMFM -preset YMF281B -k"),
        ("YMF281B",   "EXPS.vgm",    "-preset_source EXPERIMENT -preset YMF281B"),
        ("YMF281B",   "EXPM.vgm",    "-preset_source EXPERIMENT -preset YMF281B -k"),
        ("VRC7",    "YVS.vgm",      "-preset_source YMVOICE -preset VRC7"),
        ("VRC7",    "YVM.vgm",      "-preset_source YMVOICE -preset VRC7 -k"),
        ("VRC7",    "YFS.vgm",      "-preset_source YMFM -preset VRC7"),
        ("VRC7",    "YFM.vgm",      "-preset_source YMFM -preset VRC7 -k"),
        ("VRC7",    "EXPS.vgm",     "-preset_source EXPERIMENT -preset VRC7"),
        ("VRC7",    "EXPM.vgm",     "-preset_source EXPERIMENT -preset VRC7 -k"),
    ]


    with open(batch_filename, "w", encoding="utf-8") as f:
        for input_path in input_files:
            dirpath = os.path.dirname(input_path)
            basename = os.path.basename(input_path)
            # Remove 'OPLL.vgm' from the end for base name
            if basename.endswith("OPLL.vgm"):
                base_no_ext = basename[:-8]
            else:
                base_no_ext = os.path.splitext(basename)[0]

            for out_subdir, out_suffix, preset_opt in outputs:
                out_dir = os.path.join(dirpath, out_subdir)
                os.makedirs(out_dir, exist_ok=True)
                out_filename = base_no_ext + out_suffix
                out_path = os.path.join(out_dir, out_filename)
                cmd = f'./build/eseopl3patcher "{input_path}" 100 -o "{out_path}" -ch_panning 1 -detune_limit 4 {preset_opt}'
                f.write(cmd + "\n")

    print(f"Batch file '{batch_filename}' created with {len(input_files)*len(outputs)} commands.")

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: python make_batch_from_vgm.py <input_dir>")
        sys.exit(1)
    input_dir = sys.argv[1]
    main(input_dir)
