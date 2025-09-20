SHELL := /bin/bash
# 明示的に .PHONY
.PHONY: pass1 pass1-resume pass1-summary pass1-clean pass1-check

# 設定ファイル (相対パス)
CONFIG ?= config.yaml

# config.yaml の存在チェック (CRLF 等を避けるため簡潔に)
pass1-check:
	@echo "=== PASS1 CONFIG CHECK ==="
	@if [ ! -f "$(CONFIG)" ]; then \
	  echo "!! NOT FOUND: $(CONFIG) (cwd=$$(pwd))"; \
	  echo "   -> 行末 CRLF かディレクトリ違いの可能性"; \
	  exit 1; \
	else \
	  echo "Found config.yaml"; \
	fi
	@echo "Config path : $(CONFIG)"
	@echo "Corpus list : $$(./scripts/config_get.py $(CONFIG) corpus_list 2>/dev/null || echo '(key missing)')"
	@echo "Binary      : $$(./scripts/config_get.py $(CONFIG) binary 2>/dev/null || echo '(key missing)')"
	@echo "Out root    : $$(./scripts/config_get.py $(CONFIG) out_root 2>/dev/null || echo '(key missing)')"
	@echo "Jobs        : $$(./scripts/config_get.py $(CONFIG) jobs 2>/dev/null || echo '(key missing)')"
	@echo "Timeout     : $$(./scripts/config_get.py $(CONFIG) timeout_sec_per_file 2>/dev/null || echo '(key missing)')"

pass1:
	@./scripts/calib_pass1.sh -c $(CONFIG)

pass1-resume:
	@./scripts/calib_pass1.sh -c $(CONFIG) --resume

pass1-summary:
	@python3 scripts/extract_stats.py \
	  "$$(./scripts/config_get.py $(CONFIG) out_root)/stats_lines" \
	  "$$(./scripts/config_get.py $(CONFIG) out_root)/json" \
	  "$$(./scripts/config_get.py $(CONFIG) out_root)/aggregate/aggregate_stats.json"

pass1-clean:
	@OUT="$$(./scripts/config_get.py $(CONFIG) out_root 2>/dev/null || echo 'calibration/pass1_v3_baseline')"; \
	echo "Removing $$OUT"; rm -rf "$$OUT"