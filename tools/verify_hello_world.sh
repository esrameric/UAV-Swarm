#!/usr/bin/env bash
#
# Faz 0.3 — eProsima'nın "Hello World" pub/sub örneğini derleyip çalıştırır ve
# paketlerin gerçekten aktığını doğrular.
#
# Bu, Fast DDS kurulumunun uçtan uca sağlam olduğunun kanıtıdır: kütüphane
# bulunuyor, örnek derleniyor, iki ayrı süreç birbirini DDS discovery ile
# buluyor ve veri akıyor.
#
# Kullanım:  bash tools/verify_hello_world.sh

set -uo pipefail

BETIK_DIZINI="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck disable=SC1091
source "$BETIK_DIZINI/fastdds_env.sh"

ORNEK_KAYNAK="$HOME/Fast-DDS/src/fastdds/examples/cpp/hello_world"
ORNEK_SAYISI=10

echo "== Faz 0.3: Hello World pub/sub akış doğrulaması =="

if [ ! -d "$ORNEK_KAYNAK" ]; then
    echo "  [HATA] örnek kaynağı yok: $ORNEK_KAYNAK"
    echo "SONUC: BASARISIZ"; exit 1
fi

GECICI="$(mktemp -d)"
trap 'rm -rf "$GECICI"' EXIT

echo "-- örnek derleniyor..."
if ! cmake -S "$ORNEK_KAYNAK" -B "$GECICI/build" -DCMAKE_BUILD_TYPE=Release \
        >"$GECICI/cmake.log" 2>&1; then
    echo "  [HATA] cmake configure başarısız:"; tail -20 "$GECICI/cmake.log"
    echo "SONUC: BASARISIZ"; exit 1
fi
if ! cmake --build "$GECICI/build" -j3 >>"$GECICI/cmake.log" 2>&1; then
    echo "  [HATA] derleme başarısız:"; tail -20 "$GECICI/cmake.log"
    echo "SONUC: BASARISIZ"; exit 1
fi
UYGULAMA="$GECICI/build/hello_world"
[ -x "$UYGULAMA" ] || { echo "  [HATA] $UYGULAMA üretilmedi"; echo "SONUC: BASARISIZ"; exit 1; }
echo "  [OK]   örnek derlendi"

# --- abone önce başlar -------------------------------------------------------
# `&` komutu arka planda başlatır, `$!` onun süreç numarasını (PID) verir.
echo "-- abone (subscriber) başlatılıyor"
"$UYGULAMA" subscriber -s "$ORNEK_SAYISI" >"$GECICI/sub.log" 2>&1 &
abone_pid=$!

sleep 2  # abonenin DDS discovery'yi tamamlaması için kısa pay

echo "-- yayıncı (publisher) başlatılıyor"
"$UYGULAMA" publisher -s "$ORNEK_SAYISI" >"$GECICI/pub.log" 2>&1
echo "-- abonenin bitmesi bekleniyor"
wait "$abone_pid"

gonderilen="$(grep -c "SENT" "$GECICI/pub.log" 2>/dev/null || echo 0)"
alinan="$(grep -c "RECEIVED" "$GECICI/sub.log" 2>/dev/null || echo 0)"

echo
echo "----- yayıncı logu (son 3) -----"; tail -3 "$GECICI/pub.log" | sed 's/^/  /'
echo "----- abone logu (son 3) -----";   tail -3 "$GECICI/sub.log" | sed 's/^/  /'
echo "-------------------------------"
echo "  gönderilen örnek: $gonderilen"
echo "  alınan örnek:     $alinan"
echo

if [ "$gonderilen" -ge "$ORNEK_SAYISI" ] && [ "$alinan" -ge "$ORNEK_SAYISI" ]; then
    echo "SONUC: BASARILI - Fast DDS pub/sub akışı çalışıyor"
    exit 0
fi
echo "SONUC: BASARISIZ - beklenen $ORNEK_SAYISI örnek alınamadı"
exit 1
