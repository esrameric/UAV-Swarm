#!/usr/bin/env bash
# =============================================================================
#  Faz 6 — Tüm entegrasyon testlerini sırayla çalıştırır
#
#  Ön koşul: swarm_node:latest imajı derlenmiş olmalı.
#      bash tools/build_docker_image.sh
#
#  Kullanım:  bash tests/integration/run_all.sh
#             DOCKER="sudo docker" bash tests/integration/run_all.sh
#
#  Testler container ayağa kaldırdığı için sırayla (paralel değil) çalışır;
#  aynı sabit IP'leri kullandıklarından paralel çalıştırılamazlar.
# =============================================================================

set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

TESTS=(
    "test_01_discovery.sh"
    "test_02_consensus.sh"
    "test_03_consensus_iptal.sh"
    "test_04_failsafe.sh"
    "test_05_seq_sifirlama.sh"
    "test_06_rol_ayrimi.sh"
)

elapsed=0
remaining=0
failed_list=()

for test in "${TESTS[@]}"; do
    echo
    echo "############################################################"
    echo "#  $test"
    echo "############################################################"

    if bash "$SCRIPT_DIR/$test"; then
        elapsed=$((elapsed + 1))
    else
        remaining=$((remaining + 1))
        failed_list+=("$test")
    fi
done

echo
echo "############################################################"
echo "#  OZET"
echo "############################################################"
echo "  gecen test dosyasi: $elapsed"
echo "  kalan test dosyasi: $remaining"
for t in "${failed_list[@]:-}"; do
    [ -n "$t" ] && echo "    - $t"
done

if [ "$remaining" -eq 0 ]; then
    echo "SONUC: BASARILI"
    exit 0
fi
echo "SONUC: BASARISIZ"
exit 1
