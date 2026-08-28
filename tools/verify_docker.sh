#!/usr/bin/env bash
#
# Faz 0.4 — Docker + Docker Compose kurulumunu doğrular.
#
# Kullanım:  bash tools/verify_docker.sh
#
# Not: Docker daemon'a erişim için kullanıcının `docker` grubunda olması
# gerekir. Değilse docker komutu ortam değişkeniyle değiştirilebilir:
#            DOCKER="sudo docker" bash tools/verify_docker.sh

set -uo pipefail

# Docker komutu. Varsayılan `docker`; kısıtlı ortamlarda dışarıdan değiştirilir.
DOCKER="${DOCKER:-docker}"

hata_sayisi=0

basarili() { echo "  [OK]   $1"; }
basarisiz() { echo "  [HATA] $1"; hata_sayisi=$((hata_sayisi + 1)); }

echo "== Faz 0.4: Docker ortam doğrulaması =="

# --- 1) docker CLI var mı? ---------------------------------------------------
if $DOCKER --version >/dev/null 2>&1; then
    basarili "docker CLI bulundu: $($DOCKER --version)"
else
    basarisiz "docker CLI bulunamadı"
    echo "SONUC: BASARISIZ"; exit 1
fi

# --- 2) Docker Compose v2 eklentisi var mı? ----------------------------------
# Bu proje `docker compose` (v2, eklenti) kullanır; eski `docker-compose`
# (v1, ayrı python aracı) değil.
if $DOCKER compose version >/dev/null 2>&1; then
    basarili "docker compose bulundu: $($DOCKER compose version --short)"
else
    basarisiz "docker compose (v2 eklentisi) bulunamadı"
fi

# --- 3) Daemon ayakta ve erişilebilir mi? ------------------------------------
if $DOCKER info >/dev/null 2>&1; then
    basarili "docker daemon erişilebilir (server $($DOCKER version --format '{{.Server.Version}}'))"
else
    basarisiz "docker daemon'a bağlanılamıyor (kullanıcı 'docker' grubunda mı?)"
    echo "SONUC: BASARISIZ"; exit 1
fi

# --- 4) Gerçekten container çalıştırabiliyor muyuz? --------------------------
if $DOCKER run --rm hello-world >/dev/null 2>&1; then
    basarili "container çalıştırma testi (hello-world) geçti"
else
    basarisiz "hello-world container'ı çalıştırılamadı"
fi

# --- 5) Custom bridge ağı oluşturulabiliyor mu? ------------------------------
# Bölüm 4'teki mimari host networking DEĞİL, her container'a ayrı IP veren
# custom bridge ağı kullanıyor. Bunun mümkün olduğunu burada doğruluyoruz.
TEST_AG="swarm_bridge_probe"
$DOCKER network rm "$TEST_AG" >/dev/null 2>&1
if $DOCKER network create --driver bridge --subnet 172.31.250.0/24 "$TEST_AG" >/dev/null 2>&1; then
    basarili "custom bridge ağı oluşturulabiliyor"
    $DOCKER network rm "$TEST_AG" >/dev/null 2>&1
else
    basarisiz "custom bridge ağı oluşturulamadı"
fi

echo
if [ "$hata_sayisi" -eq 0 ]; then
    echo "SONUC: BASARILI - Docker ortamı hazır"
    exit 0
fi
echo "SONUC: BASARISIZ - $hata_sayisi kontrol başarısız"
exit 1
