#!/usr/bin/env bash
# =============================================================================
#  Faz 6.2 — Consensus (2PC) uçtan uca doğrulaması
#
#  Doğrulanan akış (Bölüm 3.6):
#    1) GCS gerçek bir görev emri için teklif yayınlar
#    2) Üç drone da kendi durumunu kontrol edip ACK döner
#    3) %100 ACK sağlandığı için oylama COMMITTED biter
#    4) GCS ancak BUNDAN SONRA task_alloc emrini yayınlar
#
#  Kullanım:  bash tests/integration/test_02_consensus.sh
# =============================================================================

set -uo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/common.sh"

echo "== Faz 6.2: Consensus (2PC) entegrasyon testi =="
cleanup_on_exit
stop_swarm
start_swarm

section "Sürü hazır"
verify_log gcs_node "yeni peer: id=3" 60 "GCS üç drone'u da keşfetti"

section "Aşama 1 — Teklif"
verify_log gcs_node "\[gcs\] gorev teklif ediliyor rol=SCOUT" 40 "GCS ilk görevi teklif etti (SCOUT)"
verify_log gcs_node "\[gcs\] gorev teklif ediliyor.*online drone=3" 40 "Teklif anında 3 drone da ONLINE sayıldı"
verify_log gcs_node "\[task\] gecis: IDLE -> CONSENSUS" 20 "GCS ConsensusTask'a geçti"

section "Aşama 2 — Oylar"
verify_log drone_scout     "\[consensus\] oy veriliyor tx=1 vote=ACK" 30 "Gözcü ACK verdi"
verify_log drone_striker_1 "\[consensus\] oy veriliyor tx=1 vote=ACK" 30 "Müdahale-1 ACK verdi"
verify_log drone_striker_2 "\[consensus\] oy veriliyor tx=1 vote=ACK" 30 "Müdahale-2 ACK verdi"

section "Aşama 3 — Sonuç: %100 ACK"
verify_log gcs_node "\[consensus\] sonuc tx=1 COMMITTED" 30 "Oylama COMMITTED ile bitti"
verify_log gcs_node "\[gcs\] gorev emri yayinlandi task_id=1" 30 "Görev emri oybirliğinden SONRA yayınlandı"

section "Emir sürüye ulaştı ve göreve dönüştü"
verify_log drone_scout "\[task\] gecis: IDLE -> SCOUT_SEARCH" 30 "Gözcü emri alıp arama görevine geçti"

section "İptal olmadı"
verify_log_absent gcs_node "gorev IPTAL edildi task_id=1" 2 "İlk görev iptal edilmedi"
verify_log_absent gcs_node "ABORTED" 1 "Hiçbir oylama ABORTED olmadı"

section "İkinci görev de aynı akışı izliyor"
verify_log gcs_node "\[gcs\] gorev teklif ediliyor rol=STRIKER" 40 "GCS ikinci görevi teklif etti (STRIKER)"
verify_log gcs_node "\[consensus\] sonuc tx=2 COMMITTED" 40 "İkinci oylama da COMMITTED"
verify_log gcs_node "\[gcs\] gorev emri yayinlandi task_id=2" 30 "İkinci görev emri yayınlandı"

report_result
