#!/usr/bin/env bash
# =============================================================================
#  Faz 6.3 — Oylama başarısız olduğunda görevin iptal edilmesi
#
#  İki senaryo doğrulanıyor (Bölüm 2 / 3.6):
#
#    A) NACK: bir drone'un bataryası kritik -> NACK -> tek NACK bile yeterli,
#       görev ANINDA iptal, emir YAYINLANMAZ.
#
#    B) TIMEOUT: bir drone ayakta (heartbeat yayınlıyor, ONLINE görünüyor)
#       ama consensus teklifine hiç cevap vermiyor -> 5 saniye sonra görev
#       iptal, emir YAYINLANMAZ.
#
#  İki durumun sonucu aynıdır (ABORTED) ama sebepleri farklıdır ve log'da
#  ayırt edilebilir: ABORTED_NACK / ABORTED_TIMEOUT.
#
#  Kullanım:  bash tests/integration/test_03_consensus_iptal.sh
# =============================================================================

set -uo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/common.sh"

OVERRIDES_DIR="$(dirname "${BASH_SOURCE[0]}")/overrides"

echo "== Faz 6.3: Consensus basarisizliginda gorev iptali =="
cleanup_on_exit

# ---------------------------------------------------------------------------
#  A) NACK senaryosu
# ---------------------------------------------------------------------------
section "A) Bataryasi kritik drone NACK veriyor"

EK_COMPOSE=("$OVERRIDES_DIR/dusuk_batarya.yml")
stop_swarm
start_swarm

verify_log gcs_node "yeni peer: id=3" 60 "GCS üç drone'u da keşfetti"
verify_log gcs_node "\[gcs\] gorev teklif ediliyor" 40 "GCS görev teklif etti"

verify_log drone_striker_1 "\[consensus\] oy veriliyor tx=1 vote=NACK" 30 \
    "Bataryası kritik drone NACK verdi"
verify_log drone_scout "\[consensus\] oy veriliyor tx=1 vote=ACK" 30 \
    "Sağlıklı drone ACK verdi"

verify_log gcs_node "\[consensus\] sonuc tx=1 ABORTED_NACK" 30 \
    "Oylama NACK sebebiyle iptal oldu (timeout değil)"
verify_log gcs_node "\[gcs\] gorev IPTAL edildi task_id=1" 20 \
    "GCS görevi iptal etti"
verify_log_absent gcs_node "gorev emri yayinlandi task_id=1" 2 \
    "Görev emri YAYINLANMADI"

stop_swarm

# ---------------------------------------------------------------------------
#  B) TIMEOUT senaryosu
# ---------------------------------------------------------------------------
section "B) Ayakta ama sessiz drone -> 5 saniyede zaman asimi"

EK_COMPOSE=("$OVERRIDES_DIR/sessiz_drone.yml")
start_swarm

verify_log gcs_node "yeni peer: id=3" 60 "GCS üç drone'u da keşfetti"
verify_log gcs_node "\[gcs\] gorev teklif ediliyor.*online drone=3" 40 \
    "Sessiz drone da ONLINE sayıldı (oy verecekler listesinde)"

verify_log drone_striker_2 "ARIZA SIMULASYONU" 30 \
    "Sessiz drone oy vermiyor"
verify_log drone_scout "\[consensus\] oy veriliyor tx=1 vote=ACK" 30 \
    "Diğer drone'lar ACK verdi"

verify_log gcs_node "\[consensus\] sonuc tx=1 ABORTED_TIMEOUT" 40 \
    "5 saniye sonunda oylama zaman aşımıyla iptal oldu"
verify_log gcs_node "\[gcs\] gorev IPTAL edildi task_id=1" 20 \
    "GCS görevi iptal etti"
verify_log_absent gcs_node "gorev emri yayinlandi task_id=1" 2 \
    "Görev emri YAYINLANMADI"

section "Iptal sonrasi suru IdleTask'a dondu"
verify_log gcs_node "\[task\] gecis: CONSENSUS -> IDLE" 20 \
    "GCS ConsensusTask'tan IdleTask'a döndü"
verify_log_absent drone_scout "SCOUT_SEARCH" 2 \
    "Gözcü göreve BAŞLAMADI (emir hiç yayınlanmadı)"

report_result
