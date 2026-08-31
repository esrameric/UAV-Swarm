#!/usr/bin/env bash
# =============================================================================
#  Faz 6.4 — Bir düğüm kaybolduğunda FailSafeTask tetiklenmesi
#
#  Senaryo: sürü ayağa kalkar, tanışır; sonra bir drone'un container'ı
#  `docker stop` ile durdurulur. Kalan düğümler heartbeat zaman aşımı (3 sn)
#  sonunda onu kayıp sayar; check_emergency() acil durum bildirir ve Task
#  Engine devam eden görevi iptal edip FailSafe -> Landing dizisine geçer.
#
#  Kullanım:  bash tests/integration/test_04_failsafe.sh
# =============================================================================

set -uo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/common.sh"

echo "== Faz 6.4: Dugum kaybinda FailSafe tetiklenmesi =="
cleanup_on_exit
stop_swarm
start_swarm

section "Sürü tanışıyor"
verify_log drone_scout     "yeni peer: id=3" 60 "Gözcü, Müdahale-2'yi tanıdı"
verify_log drone_striker_1 "yeni peer: id=3" 60 "Müdahale-1, Müdahale-2'yi tanıdı"

section "Bir drone'un container'i durduruluyor"
echo "-- docker stop: drone_striker_2"
compose stop -t 0 drone_striker_2 >/dev/null 2>&1

section "Kalan dugumler kaybi fark ediyor"
verify_log drone_scout     "\[peer\] kayip peer tespit edildi" 30 "Gözcü kaybı fark etti"
verify_log drone_striker_1 "\[peer\] kayip peer tespit edildi" 30 "Müdahale-1 kaybı fark etti"
verify_log gcs_node        "\[peer\] kayip peer tespit edildi" 30 "GCS kaybı fark etti"

section "FailSafe dizisi devreye giriyor"
verify_log drone_scout "\[emergency\] acil durum tespit edildi" 30 \
    "Gözcü'de acil durum tetiklendi"
verify_log drone_scout "\[task\] gecis: .* -> FAIL_SAFE" 30 \
    "Gözcü FailSafeTask'a geçti"
verify_log drone_striker_1 "\[task\] gecis: .* -> FAIL_SAFE" 30 \
    "Müdahale-1 FailSafeTask'a geçti"

section "FailSafe sonrasi inise geciliyor"
verify_log drone_scout "\[task\] gecis: FAIL_SAFE -> LANDING" 30 \
    "Gözcü FailSafe'ten LandingTask'a geçti"

report_result
