#!/usr/bin/env bash
set -euo pipefail

VGMTOOLS_REPO="${VGMTOOLS_REPO:-https://github.com/vgmrips/vgmtools.git}"
VGMPLAY_REPO="${VGMPLAY_REPO:-https://github.com/vgmrips/vgmplay.git}"

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
TOOLS_DIR="${ROOT_DIR}/tools"
SRC_DIR="${TOOLS_DIR}/_src"
BIN_DIR="${TOOLS_DIR}/bin"
VER_DIR="${TOOLS_DIR}/version"
LOG_PREFIX="[fetch_vgm_tools]"

mkdir -p "${SRC_DIR}" "${BIN_DIR}" "${VER_DIR}"

FORCE=0; NO_UPDATE=0; SKIP_VGMTXT=0; SKIP_VGMPLAY=0; QUIET=0

while [[ $# -gt 0 ]]; do
  case "$1" in
    --force) FORCE=1 ;;
    --no-update) NO_UPDATE=1 ;;
    --skip-vgm2txt) SKIP_VGMTXT=1 ;;
    --skip-vgmplay) SKIP_VGMPLAY=1 ;;
    --quiet) QUIET=1 ;;
    -h|--help)
      cat <<'EOF'
Usage: bash tools/fetch_vgm_tools.sh [options]
  --force          Force rebuild
  --no-update      Do not fetch remote changes
  --skip-vgm2txt   Skip vgm2txt
  --skip-vgmplay   Skip VGMPlay
  --quiet          Suppress most logs
EOF
      exit 0 ;;
    *) echo "Unknown option: $1" >&2; exit 1 ;;
  esac; shift
done

log(){ [[ $QUIET -eq 1 ]] || echo "${LOG_PREFIX} $*" >&2; }

clone_or_update () {
  local repo_url="$1" dest_dir="$2" commit_env="$3"
  if [[ -d "${dest_dir}/.git" ]]; then
    log "Repo $(basename "$dest_dir") exists."
    if [[ $NO_UPDATE -eq 0 && -z "${commit_env}" ]]; then
      if [[ $QUIET -eq 1 ]]; then
        (cd "$dest_dir" && git fetch --prune --tags >/dev/null 2>&1 || true)
      else
        (cd "$dest_dir" && git fetch --prune --tags || true)
      fi
    else
      log "Skip fetch (--no-update or explicit commit)."
    fi
  else
    log "Cloning $repo_url -> $dest_dir"
    if [[ $QUIET -eq 1 ]]; then
      git clone "$repo_url" "$dest_dir" >/dev/null 2>&1
    else
      git clone "$repo_url" "$dest_dir"
    fi
  fi
  if [[ -n "${commit_env}" ]]; then
    (cd "$dest_dir" && git checkout -q "${commit_env}")
  else
    # stay on current branch; fast-forward
    (cd "$dest_dir" && git rev-parse --abbrev-ref HEAD >/dev/null 2>&1 || git checkout -q "$(git symbolic-ref --short refs/remotes/origin/HEAD | sed 's|^origin/||')" )
    (cd "$dest_dir" && git pull --ff-only >/dev/null 2>&1 || true)
  fi
  (cd "$dest_dir" && git rev-parse HEAD)
}

validate_commit () {
  local c="$1"
  if [[ ! "$c" =~ ^[0-9a-fA-F]{40}$ ]]; then
    echo "ERROR: Commit hash malformed: $c" >&2
    exit 1
  fi
}

build_vgm2txt () {
  local src_dir="$1" out="${BIN_DIR}/vgm2txt"
  if [[ $FORCE -eq 0 && -x "$out" ]]; then
    log "vgm2txt exists (use --force to rebuild)."
    return
  fi
  log "Building vgm2txt"
  (cd "$src_dir" && make vgm2txt >/dev/null 2>&1 || make >/dev/null 2>&1 || { echo "vgm2txt build failed"; exit 1; })
  local cand
  cand="$(cd "$src_dir" && ls -1 vgm2txt* 2>/dev/null | head -n1 || true)"
  [[ -n "$cand" ]] || { echo "vgm2txt binary not found" >&2; exit 1; }
  cp "$src_dir/$cand" "$out"
  chmod +x "$out"
}

build_vgmplay () {
  local src_dir="$1"
  if [[ $FORCE -eq 0 && ( -x "${BIN_DIR}/VGMPlay" || -x "${BIN_DIR}/vgmplay" ) ]]; then
    log "VGMPlay exists (use --force to rebuild)."
    return
  end
  log "Building VGMPlay"
  (cd "$src_dir" && make >/dev/null 2>&1 || { echo "VGMPlay build failed"; exit 1; })
  local found
  while IFS= read -r f; do
    if file "$f" | grep -qi 'executable'; then found="$f"; break; fi
  done < <(find "$src_dir" -maxdepth 2 -type f \( -name 'VGMPlay' -o -name 'vgmplay' \))
  [[ -n "$found" ]] || { echo "VGMPlay binary not located" >&2; exit 1; }
  cp "$found" "${BIN_DIR}/$(basename "$found")"
  chmod +x "${BIN_DIR}/$(basename "$found")"
}

write_commit () {
  echo "$2" > "${VER_DIR}/$1_commit.txt"
}

update_meta () {
  local t_commit p_commit
  t_commit="$(cat "${VER_DIR}/vgm2txt_commit.txt" 2>/dev/null || true)"
  p_commit="$(cat "${VER_DIR}/vgmplay_commit.txt" 2>/dev/null || true)"
  cat > "${VER_DIR}/vgm_tools_meta.json" <<EOF
{
  "generated_at": "$(date -u +'%Y-%m-%dT%H:%M:%SZ')",
  "vgmtools_repo": "${VGMTOOLS_REPO}",
  "vgmtools_commit": "${t_commit}",
  "vgmplay_repo": "${VGMPLAY_REPO}",
  "vgmplay_commit": "${p_commit}"
}
EOF
}

[[ $SKIP_VGMTXT -eq 1 && $SKIP_VGMPLAY -eq 1 ]] && { echo "Nothing to do"; exit 1; }

vgmtools_commit=""
vgmplay_commit=""

if [[ $SKIP_VGMTXT -eq 0 ]]; then
  vgmtools_commit="$(clone_or_update "$VGMTOOLS_REPO" "${SRC_DIR}/vgmtools" "${VGMTOOLS_COMMIT:-}")"
  validate_commit "$vgmtools_commit"
  build_vgm2txt "${SRC_DIR}/vgmtools"
  write_commit vgm2txt "$vgmtools_commit"
fi

if [[ $SKIP_VGMPLAY -eq 0 ]]; then
  vgmplay_commit="$(clone_or_update "$VGMPLAY_REPO" "${SRC_DIR}/vgmplay" "${VGMPLAY_COMMIT:-}")"
  validate_commit "$vgmplay_commit"
  build_vgmplay "${SRC_DIR}/vgmplay"
  write_commit vgmplay "$vgmplay_commit"
fi

update_meta
log "Done."
if [[ $QUIET -eq 0 ]]; then
  echo "==== Summary ===="
  [[ -n "$vgmtools_commit" ]] && echo "vgm2txt commit: $vgmtools_commit"
  [[ -n "$vgmplay_commit" ]] && echo "VGMPlay commit: $vgmplay_commit"
  echo "Binaries:"
  ls -1 "${BIN_DIR}"
fi