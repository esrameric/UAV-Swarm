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
temizlikle_bitir
suruyu_durdur
suruyu_baslat

baslik "Birinci gorev: SCOUT rolune"
dogrula_log gcs_node "\[gcs\] gorev teklif ediliyor rol=SCOUT" 60 "GCS SCOUT görevi teklif etti"
dogrula_log gcs_node "\[gcs\] gorev emri yayinlandi task_id=1"  40 "Emir yayınlandı"

dogrula_log drone_scout "\[task\] gecis: IDLE -> SCOUT_SEARCH" 40 \
    "Gözcü ScoutSearchTask'a geçti"
dogrula_log_yok drone_striker_1 "GO_TO_TARGET" 2 \
    "Müdahale-1 SCOUT emrini yok saydı"
dogrula_log_yok drone_striker_2 "SCOUT_SEARCH" 1 \
    "Müdahale-2 SCOUT görevine girmedi"

baslik "Ikinci gorev: STRIKER rolune"
dogrula_log gcs_node "\[gcs\] gorev teklif ediliyor rol=STRIKER" 60 "GCS STRIKER görevi teklif etti"
dogrula_log gcs_node "\[gcs\] gorev emri yayinlandi task_id=2"   40 "İkinci emir yayınlandı"

dogrula_log drone_striker_1 "\[task\] gecis: .* -> GO_TO_TARGET" 40 \
    "Müdahale-1 GoToTargetTask'a geçti"
dogrula_log drone_striker_2 "\[task\] gecis: .* -> GO_TO_TARGET" 40 \
    "Müdahale-2 GoToTargetTask'a geçti"

baslik "GCS hicbir ucus gorevi almadi"
dogrula_log_yok gcs_node "SCOUT_SEARCH"  1 "GCS arama görevine girmedi"
dogrula_log_yok gcs_node "GO_TO_TARGET"  1 "GCS hedefe gidiş görevine girmedi"

baslik "Ayni emir role gore FARKLI goreve donustu"
scout_arama="$(log_say drone_scout 'SCOUT_SEARCH')"
striker_gidis="$(log_say drone_striker_1 'GO_TO_TARGET')"
echo "     Gözcü'de SCOUT_SEARCH satiri: $scout_arama"
echo "     Müdahale-1'de GO_TO_TARGET satiri: $striker_gidis"

if [ "$scout_arama" -gt 0 ] && [ "$striker_gidis" -gt 0 ]; then
    gecti "Heterojen rol ayrımı çalışıyor"
else
    kaldi "Rollere göre farklı görev üretilmedi"
fi

sonucu_bildir
