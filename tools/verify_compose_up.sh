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
DEPO_KOKU="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
COMPOSE_DOSYASI="$DEPO_KOKU/docker/docker-compose.yml"
PROJE="swarmup"

SERVISLER=(gcs_node drone_scout drone_striker_1 drone_striker_2)

hata_sayisi=0
gecti() { echo "  [OK]   $1"; }
kaldi() { echo "  [HATA] $1"; hata_sayisi=$((hata_sayisi + 1)); }

compose() { $DOCKER compose -p "$PROJE" -f "$COMPOSE_DOSYASI" "$@"; }

temizle() {
    compose down -v --remove-orphans >/dev/null 2>&1
}
trap temizle EXIT

echo "== Faz 5.3: 4 container ayaga kalkiyor mu =="

# İmaj var mı?
if ! $DOCKER image inspect swarm_node:latest >/dev/null 2>&1; then
    echo "  [HATA] swarm_node:latest imaji yok."
    echo "         Once: bash tools/build_docker_image.sh"
    echo "SONUC: BASARISIZ"
    exit 1
fi
gecti "swarm_node:latest imaji mevcut"

temizle
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
for servis in "${SERVISLER[@]}"; do
    kimlik="$(compose ps -q "$servis" 2>/dev/null)"

    if [ -z "$kimlik" ]; then
        kaldi "$servis: container olusturulmamis"
        continue
    fi

    durum="$($DOCKER inspect -f '{{.State.Status}}' "$kimlik" 2>/dev/null)"
    ip="$($DOCKER inspect -f '{{range .NetworkSettings.Networks}}{{.IPAddress}}{{end}}' "$kimlik" 2>/dev/null)"

    if [ "$durum" != "running" ]; then
        cikis_kodu="$($DOCKER inspect -f '{{.State.ExitCode}}' "$kimlik" 2>/dev/null)"
        kaldi "$servis: ayakta degil (durum=$durum, cikis kodu=$cikis_kodu)"
        echo "         son loglar:"
        compose logs --no-color --tail 10 "$servis" 2>/dev/null | sed 's/^/           /'
        continue
    fi

    # "hazir" satırı, 3 thread'in başladığını ve DDS'in kurulduğunu gösterir.
    if compose logs --no-color "$servis" 2>/dev/null | grep -q "\[node\] hazir"; then
        gecti "$servis: ayakta ve hazir  (ip=$ip)"
    else
        kaldi "$servis: ayakta ama '[node] hazir' satiri yok  (ip=$ip)"
    fi
done

echo
if [ "$hata_sayisi" -eq 0 ]; then
    echo "SONUC: BASARILI - 4 container ayakta"
    exit 0
fi
echo "SONUC: BASARISIZ - $hata_sayisi container sorunlu"
exit 1
