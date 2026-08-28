#!/usr/bin/env bash
#
# Faz 5.1 — swarm_node Docker imajını derler.
#
# Doğrudan `docker build` yerine bu script kullanılır çünkü kurumsal
# TLS-inspection proxy'si arkasındaki makinelerde host'un kök sertifikalarının
# build context'ine kopyalanması gerekir (bkz. docker/ca-certificates/README.md).
# Proxy yoksa script fazladan hiçbir şey yapmaz.
#
# Kullanım:  bash tools/build_docker_image.sh
#            DOCKER="sudo docker" bash tools/build_docker_image.sh

set -uo pipefail

DOCKER="${DOCKER:-docker}"
DEPO_KOKU="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SERTIFIKA_DIZINI="$DEPO_KOKU/docker/ca-certificates"
HOST_SERTIFIKALARI="/usr/local/share/ca-certificates"

kopyalanan_sertifikalar=()

temizle() {
    # Build bittikten sonra kopyalanan sertifikaları depodan siliyoruz;
    # kuruma özgü dosyalar çalışma ağacında kalmasın.
    local dosya
    for dosya in "${kopyalanan_sertifikalar[@]:-}"; do
        [ -n "$dosya" ] && rm -f "$dosya"
    done
}
trap temizle EXIT

mkdir -p "$SERTIFIKA_DIZINI"

if [ -d "$HOST_SERTIFIKALARI" ]; then
    shopt -s nullglob
    for sertifika in "$HOST_SERTIFIKALARI"/*.crt; do
        hedef="$SERTIFIKA_DIZINI/$(basename "$sertifika")"
        cp "$sertifika" "$hedef" 2>/dev/null || continue
        kopyalanan_sertifikalar+=("$hedef")
    done
    shopt -u nullglob
fi

if [ "${#kopyalanan_sertifikalar[@]}" -gt 0 ]; then
    echo "-- ${#kopyalanan_sertifikalar[@]} adet kurumsal kok sertifikasi build context'ine kopyalandi"
else
    echo "-- ek kok sertifikasi yok (kurumsal proxy arkasinda degilsiniz)"
fi

echo "-- imaj derleniyor: swarm_node:latest"
$DOCKER build -f "$DEPO_KOKU/docker/Dockerfile" -t swarm_node:latest "$DEPO_KOKU"
sonuc=$?

if [ $sonuc -eq 0 ]; then
    echo "SONUC: BASARILI - swarm_node:latest hazir"
else
    echo "SONUC: BASARISIZ - docker build $sonuc koduyla dondu"
fi
exit $sonuc
