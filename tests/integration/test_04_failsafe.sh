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
temizlikle_bitir
suruyu_durdur
suruyu_baslat

baslik "Sürü tanışıyor"
dogrula_log drone_scout     "yeni peer: id=3" 60 "Gözcü, Müdahale-2'yi tanıdı"
dogrula_log drone_striker_1 "yeni peer: id=3" 60 "Müdahale-1, Müdahale-2'yi tanıdı"

baslik "Bir drone'un container'i durduruluyor"
echo "-- docker stop: drone_striker_2"
compose stop -t 0 drone_striker_2 >/dev/null 2>&1

baslik "Kalan dugumler kaybi fark ediyor"
dogrula_log drone_scout     "\[peer\] kayip peer tespit edildi" 30 "Gözcü kaybı fark etti"
dogrula_log drone_striker_1 "\[peer\] kayip peer tespit edildi" 30 "Müdahale-1 kaybı fark etti"
dogrula_log gcs_node        "\[peer\] kayip peer tespit edildi" 30 "GCS kaybı fark etti"

baslik "FailSafe dizisi devreye giriyor"
dogrula_log drone_scout "\[emergency\] acil durum tespit edildi" 30 \
    "Gözcü'de acil durum tetiklendi"
dogrula_log drone_scout "\[task\] gecis: .* -> FAIL_SAFE" 30 \
    "Gözcü FailSafeTask'a geçti"
dogrula_log drone_striker_1 "\[task\] gecis: .* -> FAIL_SAFE" 30 \
    "Müdahale-1 FailSafeTask'a geçti"

baslik "FailSafe sonrasi inise geciliyor"
dogrula_log drone_scout "\[task\] gecis: FAIL_SAFE -> LANDING" 30 \
    "Gözcü FailSafe'ten LandingTask'a geçti"

sonucu_bildir
