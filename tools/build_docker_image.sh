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
REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CERT_DIR="$REPO_ROOT/docker/ca-certificates"
HOST_CERTS="/usr/local/share/ca-certificates"

copied_certs=()

cleanup() {
    # Build bittikten sonra kopyalanan sertifikaları depodan siliyoruz;
    # kuruma özgü dosyalar çalışma ağacında kalmasın.
    local dosya
    for dosya in "${copied_certs[@]:-}"; do
        [ -n "$dosya" ] && rm -f "$dosya"
    done
}
trap cleanup EXIT

mkdir -p "$CERT_DIR"

if [ -d "$HOST_CERTS" ]; then
    shopt -s nullglob
    for cert in "$HOST_CERTS"/*.crt; do
        dest="$CERT_DIR/$(basename "$cert")"
        cp "$cert" "$dest" 2>/dev/null || continue
        copied_certs+=("$dest")
    done
    shopt -u nullglob
fi

if [ "${#copied_certs[@]}" -gt 0 ]; then
    echo "-- ${#copied_certs[@]} adet kurumsal kok sertifikasi build context'ine kopyalandi"
else
    echo "-- ek kok sertifikasi yok (kurumsal proxy arkasinda degilsiniz)"
fi

echo "-- imaj derleniyor: swarm_node:latest"
$DOCKER build -f "$REPO_ROOT/docker/Dockerfile" -t swarm_node:latest "$REPO_ROOT"
result=$?

if [ $result -eq 0 ]; then
    echo "SONUC: BASARILI - swarm_node:latest hazir"
else
    echo "SONUC: BASARISIZ - docker build $result koduyla dondu"
fi
exit $result
