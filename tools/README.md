# External Tools (Optional)

This project can optionally use helper tools for inspection:

| Tool    | Purpose              | Required? |
|---------|----------------------|-----------|
| vgm2txt | Human-readable diff  | No (tests still run) |
| VGMPlay | Audition / listening | No |

## Installation (Linux / WSL)

```
./tools/fetch_vgm_tools.sh vgm2txt
./tools/fetch_vgm_tools.sh vgmplay
```

This will place binaries under `tools/bin/` and add a symlink (if possible).

Add to PATH for the current shell:
```
export PATH="$PWD/tools/bin:$PATH"
```

## Manual Build (Alternative)

- vgm2txt: clone ValleyBell/vgmtools and run `make -C vgmtools vgm2txt`
- VGMPlay: clone ValleyBell/VGMPlay and run `make`

## Updating

Re-run the fetch script with the same argument; it will overwrite the old version.

## License Notes

The binaries are NOT committed to the repository. Refer to upstream license files in their respective source repositories.


# vgm_ir (prototype): VGM -> IR extractor + utilities

Updates in this version
- New per-write timeline CSV that matches your expectation:
  - 1 VGM register write = 1 row
  - time origin at the first write (time=0), ticks=ceil(time*60) with the special-case ticks==1 -> 0
  - Register-aware type column (PSG: fCA/fCB/aVC/mode/wNC/envL/envM/envS/ioP1/ioP2; SCC: wtb/f1/f2/vol/en)
  - Optional convenience columns (reg16/pitch_hz/vol4) are included; raw reg/dd always present

CLI
```bash
# Per-write timeline (for diff against your expected CSV)
python -m tools.vgm_ir.vgm2ir input.vgm --out analysis/ir --timeline-per-write

# Aggregated (previous) f/v/fV timeline is still available:
python -m tools.vgm_ir.vgm2ir input.vgm --out analysis/ir --timeline-quant
```

Per-write file format
```
chip,ch,tick,time_s,type,reg,dd,reg16,pitch_hz,vol4,samples
# samples is relative samples from the first register write
```

Notes
- The previous aggregated timeline deduplicated unchanged values; the new per-write timeline does not deduplicate and therefore preserves 1:1 parity with VGM writes, resolving the “one-row shift”.
- time and tick are computed with a fixed 44100.0 base to align with openMSX vgmrecorder.