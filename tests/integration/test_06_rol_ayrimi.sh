#!/usr/bin/env bash
# =============================================================================
#  Faz 6.6 — Heterojen rol ayrımının doğru task'ları tetiklemesi
#
#  Doğrulanan (Bölüm 2 / 3.3): görev emri bir DRONE'a değil bir ROLE
#  gönderilir. TaskAllocationEngine emri alıp rolüne göre farklı child task
#  üretir:
#      SCOUT   -> ScoutSearchTask
#      STRIKER -> GoToTargetTask
#  Rolü tutmayan drone emri sessizce yok sayar; GCS hiç uçuş görevi almaz.
#
#  GCS senaryosu sırayla iki görev teklif eder (önce SCOUT, sonra STRIKER),
#  bu yüzden tek bir çalıştırmada iki rol de gözlemlenebiliyor.
#
#  Kullanım:  bash tests/integration/test_06_rol_ayrimi.sh
# =============================================================================

set -uo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/common.sh"

echo "== Faz 6.6: Heterojen rol ayrimi =="
cleanup_on_exit
stop_swarm
start_swarm

section "Birinci gorev: SCOUT rolune"
verify_log gcs_node "\[gcs\] gorev teklif ediliyor rol=SCOUT" 60 "GCS SCOUT görevi teklif etti"
verify_log gcs_node "\[gcs\] gorev emri yayinlandi task_id=1"  40 "Emir yayınlandı"

verify_log drone_scout "\[task\] gecis: IDLE -> SCOUT_SEARCH" 40 \
    "Gözcü ScoutSearchTask'a geçti"
verify_log_absent drone_striker_1 "GO_TO_TARGET" 2 \
    "Müdahale-1 SCOUT emrini yok saydı"
verify_log_absent drone_striker_2 "SCOUT_SEARCH" 1 \
    "Müdahale-2 SCOUT görevine girmedi"

section "Ikinci gorev: STRIKER rolune"
verify_log gcs_node "\[gcs\] gorev teklif ediliyor rol=STRIKER" 60 "GCS STRIKER görevi teklif etti"
verify_log gcs_node "\[gcs\] gorev emri yayinlandi task_id=2"   40 "İkinci emir yayınlandı"

verify_log drone_striker_1 "\[task\] gecis: .* -> GO_TO_TARGET" 40 \
    "Müdahale-1 GoToTargetTask'a geçti"
verify_log drone_striker_2 "\[task\] gecis: .* -> GO_TO_TARGET" 40 \
    "Müdahale-2 GoToTargetTask'a geçti"

section "GCS hicbir ucus gorevi almadi"
verify_log_absent gcs_node "SCOUT_SEARCH"  1 "GCS arama görevine girmedi"
verify_log_absent gcs_node "GO_TO_TARGET"  1 "GCS hedefe gidiş görevine girmedi"

section "Ayni emir role gore FARKLI goreve donustu"
scout_arama="$(count_log drone_scout 'SCOUT_SEARCH')"
striker_gidis="$(count_log drone_striker_1 'GO_TO_TARGET')"
echo "     Gözcü'de SCOUT_SEARCH satiri: $scout_arama"
echo "     Müdahale-1'de GO_TO_TARGET satiri: $striker_gidis"

if [ "$scout_arama" -gt 0 ] && [ "$striker_gidis" -gt 0 ]; then
    ok "Heterojen rol ayrımı çalışıyor"
else
    fail "Rollere göre farklı görev üretilmedi"
fi

report_result
