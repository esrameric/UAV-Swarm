#!/usr/bin/env bash
#
# Faz 0.5 — Custom bridge ağında UDP multicast'in gerçekten çalıştığını doğrular.
#
# NEDEN KRİTİK: Sistemin heartbeat ve telemetri akışları (Bölüm 3.4) UDP
# multicast üzerinden gidiyor; Fast DDS'in discovery mekanizması (SPDP) de
# 239.255.0.1:7400 multicast adresini kullanıyor. Docker'ın bridge ağlarında
# multicast bazen kısıtlanabildiği için, mimariyi kodlamadan ÖNCE bunun
# çalıştığını kanıtlamamız gerekiyor. Çalışmazsa ağ stratejisi revize edilir.
#
# Yöntem: Bölüm 4'tekiyle aynı parametrelerle geçici bir bridge ağı kurulur,
# iki container ayrı IP'lerle bu ağa bağlanır; biri multicast yayınlar, diğeri
# gruba katılıp dinler. Alıcı paketi alırsa test geçer.
#
# Kullanım:  bash tools/verify_multicast.sh
#            (docker daemon erişimi gerekir)
#
# Kullanıcı `docker` grubunda değilse docker komutu dışarıdan değiştirilebilir:
#            DOCKER="sudo docker" bash tools/verify_multicast.sh

set -uo pipefail

BETIK_DIZINI="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SONDA_DIZINI="$BETIK_DIZINI/multicast_check"

AG_ADI="swarm_mcast_test"
ALICI="swarm_mcast_alici"
GONDERICI="swarm_mcast_gonderici"
IMAJ="python:3.12-alpine"

# Docker komutu. Varsayılan `docker`; kullanıcı `docker` grubunda değilse
# ortam değişkeniyle değiştirilebilir (bkz. yukarıdaki kullanım notu).
DOCKER="${DOCKER:-docker}"

# Bölüm 4'teki docker-compose ağıyla birebir aynı parametreler.
SUBNET="172.20.0.0/16"
ALICI_IP="172.20.0.21"
GONDERICI_IP="172.20.0.22"

temizle() {
    $DOCKER rm -f "$ALICI" "$GONDERICI" >/dev/null 2>&1
    $DOCKER network rm "$AG_ADI" >/dev/null 2>&1
}

# `trap ... EXIT`: script hangi sebeple biterse bitsin (başarı, hata, Ctrl-C)
# temizle() mutlaka çalışsın. Arkada başıboş container/ağ bırakmamak için.
trap temizle EXIT

echo "== Faz 0.5: Custom bridge ağında multicast doğrulaması =="

temizle  # önceki yarım kalmış çalışmalardan kalıntı varsa sil

echo "-- ağ oluşturuluyor: $AG_ADI ($SUBNET)"
if ! $DOCKER network create --driver bridge --subnet "$SUBNET" "$AG_ADI" >/dev/null; then
    echo "SONUC: BASARISIZ - bridge ağı oluşturulamadı"
    exit 1
fi

echo "-- alıcı başlatılıyor ($ALICI_IP)"
$DOCKER run -d --name "$ALICI" \
    --network "$AG_ADI" --ip "$ALICI_IP" \
    -v "$SONDA_DIZINI:/sonda:ro" \
    "$IMAJ" python3 /sonda/receiver.py >/dev/null

# Alıcının multicast grubuna katılması (IGMP join) için kısa bir pay bırakıyoruz;
# yoksa gönderici, alıcı hazır olmadan yayına başlayabilir.
sleep 3

echo "-- gönderici başlatılıyor ($GONDERICI_IP)"
$DOCKER run -d --name "$GONDERICI" \
    --network "$AG_ADI" --ip "$GONDERICI_IP" \
    -v "$SONDA_DIZINI:/sonda:ro" \
    "$IMAJ" python3 /sonda/sender.py >/dev/null

echo "-- alıcının sonucu bekleniyor..."
alici_cikis="$($DOCKER wait "$ALICI" 2>/dev/null)"

echo
echo "----- alıcı logu -----"
$DOCKER logs "$ALICI" 2>&1 | sed 's/^/  /'
echo "----- gönderici logu -----"
$DOCKER logs "$GONDERICI" 2>&1 | sed 's/^/  /'
echo "----------------------"
echo

if [ "$alici_cikis" = "0" ]; then
    echo "SONUC: BASARILI - bridge ağında multicast çalışıyor"
    exit 0
fi

cat <<'EOF'
SONUC: BASARISIZ - bridge ağında multicast paketi teslim edilemedi

Olası çözümler (Bölüm 4):
  - docker-compose ağına driver_opts ekleyin, örn:
      com.docker.network.bridge.enable_ip_masquerade: "true"
  - bridge yerine macvlan sürücüsünü deneyin
  - host çekirdeğinde multicast/IGMP ayarlarını kontrol edin:
      sysctl net.ipv4.conf.all.mc_forwarding
EOF
exit 1
