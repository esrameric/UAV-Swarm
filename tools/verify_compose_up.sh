#!/usr/bin/env bash
#
# Faz 5.3 — 4 container'ın (GCS + 3 drone) gerçekten ayağa kalktığını doğrular.
#
# Faz 6'daki entegrasyon testlerinin ön koşulu budur: sistem davranışını
# sınamadan önce container'ların ayakta kaldığından emin olmak gerekir.
# Çöken bir container'ı "davranış hatası" sanıp saatlerce aramamak için.
#
# Kullanım:  bash tools/verify_compose_up.sh
#            DOCKER="sudo docker" bash tools/verify_compose_up.sh

set -uo pipefail

DOCKER="${DOCKER:-docker}"
REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
COMPOSE_FILE="$REPO_ROOT/docker/docker-compose.yml"
PROJECT="swarmup"

SERVICES=(gcs_node drone_scout drone_striker_1 drone_striker_2)

error_count=0
ok() { echo "  [OK]   $1"; }
fail() { echo "  [HATA] $1"; error_count=$((error_count + 1)); }

compose() { $DOCKER compose -p "$PROJECT" -f "$COMPOSE_FILE" "$@"; }

cleanup() {
    compose down -v --remove-orphans >/dev/null 2>&1
}
trap cleanup EXIT

echo "== Faz 5.3: 4 container ayaga kalkiyor mu =="

# İmaj var mı?
if ! $DOCKER image inspect swarm_node:latest >/dev/null 2>&1; then
    echo "  [HATA] swarm_node:latest imaji yok."
    echo "         Once: bash tools/build_docker_image.sh"
    echo "SONUC: BASARISIZ"
    exit 1
fi
ok "swarm_node:latest imaji mevcut"

cleanup
echo "-- container'lar baslatiliyor"
if ! compose up -d --no-build >/dev/null 2>&1; then
    echo "  [HATA] docker compose up basarisiz"
    echo "SONUC: BASARISIZ"
    exit 1
fi

# Düğümlerin kurulumu tamamlaması için pay bırakıyoruz.
echo "-- 15 saniye calismasi bekleniyor"
sleep 15

echo
echo "-- durum kontrolu"
for service in "${SERVICES[@]}"; do
    id="$(compose ps -q "$service" 2>/dev/null)"

    if [ -z "$id" ]; then
        fail "$service: container olusturulmamis"
        continue
    fi

    state="$($DOCKER inspect -f '{{.State.Status}}' "$id" 2>/dev/null)"
    ip="$($DOCKER inspect -f '{{range .NetworkSettings.Networks}}{{.IPAddress}}{{end}}' "$id" 2>/dev/null)"

    if [ "$state" != "running" ]; then
        exit_code="$($DOCKER inspect -f '{{.State.ExitCode}}' "$id" 2>/dev/null)"
        fail "$service: ayakta degil (durum=$state, cikis kodu=$exit_code)"
        echo "         son loglar:"
        compose logs --no-color --tail 10 "$service" 2>/dev/null | sed 's/^/           /'
        continue
    fi

    # "hazir" satırı, 3 thread'in başladığını ve DDS'in kurulduğunu gösterir.
    if compose logs --no-color "$service" 2>/dev/null | grep -q "\[node\] hazir"; then
        ok "$service: ayakta ve hazir  (ip=$ip)"
    else
        fail "$service: ayakta ama '[node] hazir' satiri yok  (ip=$ip)"
    fi
done

echo
if [ "$error_count" -eq 0 ]; then
    echo "SONUC: BASARILI - 4 container ayakta"
    exit 0
fi
echo "SONUC: BASARISIZ - $error_count container sorunlu"
exit 1
