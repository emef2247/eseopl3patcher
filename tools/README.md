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

