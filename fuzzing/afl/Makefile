SHELL := /bin/bash
.DEFAULT_GOAL := help

PROJECT_ROOT := $(abspath ../..)
PLAIN_BUILD_DIR := $(PROJECT_ROOT)/cmake-build-afl
ASAN_BUILD_DIR := $(PROJECT_ROOT)/cmake-build-afl-asan
AFL_INSTANCES ?= 4

.PHONY: help check all build asan run fresh run-asan parallel parallel-asan \
        calendar-build calendar-run calendar-fresh calendar-crashes calendar-hangs \
        status crashes hangs replay clean clean-build clean-all

help:
	@printf '%s\n' \
		'Styles 4 Dogs - AFL++' \
		'' \
		'make / make build               AFL-Build erstellen' \
		'make run                        Build erstellen/aktualisieren und AFL starten' \
		'make fresh                      Ergebnisse löschen und neuen Lauf starten' \
		'make asan                       AFL-Build mit ASan/UBSan erstellen' \
		'make run-asan                   ASan/UBSan-AFL-Build starten' \
		'make parallel AFL_INSTANCES=4   Mehrere AFL-Instanzen starten' \
		'make parallel-asan              Mehrere ASan/UBSan-Instanzen starten' \
		'make calendar-build             Kalender-Verfügbarkeitsziel bauen' \
		'make calendar-run               Kalender-Verfügbarkeitsziel fuzzing' \
		'make calendar-fresh             Neuen Kalender-Fuzzing-Lauf starten' \
		'make calendar-crashes           Kalender-Crashes auflisten' \
		'make calendar-hangs             Kalender-Hänger auflisten' \
		'make status                     AFL-Status anzeigen' \
		'make crashes                    Gefundene Crashes auflisten' \
		'make hangs                      Gefundene Hänger auflisten' \
		'make replay FILE=<Datei>        Crash mit ASan/UBSan wiederholen' \
		'make clean                      AFL-Ausgaben löschen' \
		'make clean-build                AFL-Build-Verzeichnisse löschen' \
		'make clean-all                  Ausgaben und Builds löschen'

check:
	@command -v afl-fuzz >/dev/null || { echo 'Fehlt: afl-fuzz (Paket afl++)' >&2; exit 1; }
	@command -v afl-clang-fast >/dev/null || { echo 'Fehlt: afl-clang-fast (Paket afl++)' >&2; exit 1; }
	@command -v afl-showmap >/dev/null || { echo 'Fehlt: afl-showmap (Paket afl++)' >&2; exit 1; }
	@command -v cmake >/dev/null || { echo 'Fehlt: cmake' >&2; exit 1; }
	@command -v ninja >/dev/null || { echo 'Fehlt: ninja' >&2; exit 1; }
	@command -v pkg-config >/dev/null || { echo 'Fehlt: pkg-config' >&2; exit 1; }
	@pkg-config --exists libsodium || { echo 'Fehlt: libsodium für pkg-config' >&2; exit 1; }
	@pkg-config --exists sqlite3 || { echo 'Fehlt: sqlite3 für pkg-config' >&2; exit 1; }
	@pkg-config --exists libcurl || { echo 'Fehlt: libcurl für pkg-config' >&2; exit 1; }

all: build

build: check
	./build.sh plain

asan: check
	./build.sh asan

run: build
	./run.sh

fresh: clean build
	./run.sh

run-asan: asan
	AFL_BUILD_DIR="$(ASAN_BUILD_DIR)" ./run.sh

parallel: build
	AFL_INSTANCES="$(AFL_INSTANCES)" ./run_parallel.sh

parallel-asan: asan
	AFL_BUILD_DIR="$(ASAN_BUILD_DIR)" AFL_INSTANCES="$(AFL_INSTANCES)" ./run_parallel.sh

calendar-build: check
	./build_calendar.sh

calendar-run: calendar-build
	./run_calendar.sh

calendar-fresh: clean calendar-build
	./run_calendar.sh

calendar-crashes:
	@find calendar_out -path '*/crashes/*' -type f ! -name README.txt 2>/dev/null || true

calendar-hangs:
	@find calendar_out -path '*/hangs/*' -type f ! -name README.txt 2>/dev/null || true

status:
	./status.sh

crashes:
	@find out sync -path '*/crashes/*' -type f ! -name README.txt 2>/dev/null || true

hangs:
	@find out sync -path '*/hangs/*' -type f ! -name README.txt 2>/dev/null || true

replay:
	@if [[ -z "$(FILE)" ]]; then \
		echo 'Verwendung: make replay FILE=<AFL-Crashdatei>' >&2; \
		exit 2; \
	fi
	./replay_crash.sh "$(FILE)"

clean:
	./clean.sh

clean-build:
	rm -rf -- "$(PLAIN_BUILD_DIR)" "$(ASAN_BUILD_DIR)"

clean-all: clean clean-build
