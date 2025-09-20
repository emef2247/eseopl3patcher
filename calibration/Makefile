# Calibration 専用 Makefile
# ルートの patcher バイナリ (./eseopl3patcher_*) がビルド済み前提
# RMAP_PROFILE 環境変数で RateMap 選択: SIMPLE / calibv2 / calibv3

SHELL := /bin/bash

PASS1_REPORTS := $(addprefix reports/report_,$(addsuffix _pass1.json,$(shell awk '/bases:/,/^$$/ {if($$1 ~ /-/) {gsub("-","");print $$2}}' config.yaml)))
PASS2_REPORTS := $(addprefix reports/report_,$(addsuffix _pass2.json,$(shell awk '/bases:/,/^$$/ {if($$1 ~ /-/) {gsub("-","");print $$2}}' config.yaml)))

.PHONY: pass1 pass2 balance summarize clean

pass1:
	@echo "[CALIB] PASS1 (RMAP_PROFILE=$${RMAP_PROFILE:-SIMPLE})"
	@./scripts/calib_pass1.sh
	@python3 ./scripts/summarize_enhanced.py --pass pass1 --warn --out reports/summarize_pass1.csv > reports/warn_pass1.txt

balance:
	@echo "[CALIB] Harmonic balance from PASS1"
	@python3 ./scripts/harmonic_balance.py

pass2: balance
	@echo "[CALIB] PASS2 (apply overrides)"
	@./scripts/calib_pass2.sh
	@python3 ./scripts/summarize_enhanced.py --pass pass2 --warn --out reports/summarize_pass2.csv > reports/warn_pass2.txt

summarize:
	@python3 ./scripts/summarize_enhanced.py --pass pass1 --warn
	@python3 ./scripts/summarize_enhanced.py --pass pass2 --warn || true

clean:
	rm -f reports/report_*_pass*.json reports/summarize_pass*.csv reports/warn_pass*.txt overrides/overrides_pass2.json
	rm -f p??o4_*_pass1.vgm p??o4_*_pass2.vgm p??o4_*_pass1.wav p??o4_*_pass2.wav