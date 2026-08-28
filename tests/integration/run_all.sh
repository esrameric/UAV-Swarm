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

BETIK_DIZINI="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

TESTLER=(
    "test_01_discovery.sh"
    "test_02_consensus.sh"
    "test_03_consensus_iptal.sh"
    "test_04_failsafe.sh"
    "test_05_seq_sifirlama.sh"
    "test_06_rol_ayrimi.sh"
)

gecen=0
kalan=0
kalanlar=()

for test in "${TESTLER[@]}"; do
    echo
    echo "############################################################"
    echo "#  $test"
    echo "############################################################"

    if bash "$BETIK_DIZINI/$test"; then
        gecen=$((gecen + 1))
    else
        kalan=$((kalan + 1))
        kalanlar+=("$test")
    fi
done

echo
echo "############################################################"
echo "#  OZET"
echo "############################################################"
echo "  gecen test dosyasi: $gecen"
echo "  kalan test dosyasi: $kalan"
for t in "${kalanlar[@]:-}"; do
    [ -n "$t" ] && echo "    - $t"
done

if [ "$kalan" -eq 0 ]; then
    echo "SONUC: BASARILI"
    exit 0
fi
echo "SONUC: BASARISIZ"
exit 1
