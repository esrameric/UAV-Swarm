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

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck disable=SC1091
source "$SCRIPT_DIR/fastdds_env.sh"

EXAMPLE_SOURCE="$HOME/Fast-DDS/src/fastdds/examples/cpp/hello_world"
SAMPLE_COUNT=10

echo "== Faz 0.3: Hello World pub/sub akış doğrulaması =="

if [ ! -d "$EXAMPLE_SOURCE" ]; then
    echo "  [HATA] örnek kaynağı yok: $EXAMPLE_SOURCE"
    echo "SONUC: BASARISIZ"; exit 1
fi

TEMP_DIR="$(mktemp -d)"
trap 'rm -rf "$GECICI"' EXIT

echo "-- örnek derleniyor..."
if ! cmake -S "$EXAMPLE_SOURCE" -B "$TEMP_DIR/build" -DCMAKE_BUILD_TYPE=Release \
        >"$TEMP_DIR/cmake.log" 2>&1; then
    echo "  [HATA] cmake configure başarısız:"; tail -20 "$TEMP_DIR/cmake.log"
    echo "SONUC: BASARISIZ"; exit 1
fi
if ! cmake --build "$TEMP_DIR/build" -j3 >>"$TEMP_DIR/cmake.log" 2>&1; then
    echo "  [HATA] derleme başarısız:"; tail -20 "$TEMP_DIR/cmake.log"
    echo "SONUC: BASARISIZ"; exit 1
fi
APP="$TEMP_DIR/build/hello_world"
[ -x "$APP" ] || { echo "  [HATA] $APP üretilmedi"; echo "SONUC: BASARISIZ"; exit 1; }
echo "  [OK]   örnek derlendi"

# --- abone önce başlar -------------------------------------------------------
# `&` komutu arka planda başlatır, `$!` onun süreç numarasını (PID) verir.
echo "-- abone (subscriber) başlatılıyor"
"$APP" subscriber -s "$SAMPLE_COUNT" >"$TEMP_DIR/sub.log" 2>&1 &
subscriber_pid=$!

sleep 2  # abonenin DDS discovery'yi tamamlaması için kısa pay

echo "-- yayıncı (publisher) başlatılıyor"
"$APP" publisher -s "$SAMPLE_COUNT" >"$TEMP_DIR/pub.log" 2>&1
echo "-- abonenin bitmesi bekleniyor"
wait "$subscriber_pid"

sent="$(grep -c "SENT" "$TEMP_DIR/pub.log" 2>/dev/null || echo 0)"
received="$(grep -c "RECEIVED" "$TEMP_DIR/sub.log" 2>/dev/null || echo 0)"

echo
echo "----- yayıncı logu (son 3) -----"; tail -3 "$TEMP_DIR/pub.log" | sed 's/^/  /'
echo "----- abone logu (son 3) -----";   tail -3 "$TEMP_DIR/sub.log" | sed 's/^/  /'
echo "-------------------------------"
echo "  gönderilen örnek: $sent"
echo "  alınan örnek:     $received"
echo

if [ "$sent" -ge "$SAMPLE_COUNT" ] && [ "$received" -ge "$SAMPLE_COUNT" ]; then
    echo "SONUC: BASARILI - Fast DDS pub/sub akışı çalışıyor"
    exit 0
fi
echo "SONUC: BASARISIZ - beklenen $SAMPLE_COUNT örnek alınamadı"
exit 1
