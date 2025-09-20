#!/usr/bin/env python3
import sys, re, os

if len(sys.argv) < 3:
    print("usage: config_get.py <config.yaml> <key>", file=sys.stderr)
    sys.exit(1)

path, key = sys.argv[1], sys.argv[2]
if not os.path.isfile(path):
    print(f"missing:{path}", file=sys.stderr)
    sys.exit(2)

# 行頭 key: の単純パターン
pat = re.compile(r'^\s*' + re.escape(key) + r'\s*:\s*(.+?)\s*$')

raw_val = None
with open(path, 'r', encoding='utf-8') as f:
    for line in f:
        m = pat.match(line)
        if m:
            raw_val = m.group(1)
            break

if raw_val is None:
    sys.exit(3)

val = raw_val.strip()

# インラインコメント除去（値の途中に # を含めたい高度ケースは今は非対応で OK）
# 空白 + # 以降を削る
if '#' in val:
    # 先頭が '#' ではない場合のみ分割
    parts = val.split('#', 1)
    val = parts[0].rstrip()

# クォート除去
if (val.startswith('"') and val.endswith('"')) or (val.startswith("'") and val.endswith("'")):
    val = val[1:-1]

print(val)