#!/usr/bin/env bash
#
# Faz 0.2 — fastddsgen (IDL -> C++ derleyicisi) çalışıyor mu doğrular.
#
# fastddsgen bir JAVA uygulamasıdır; bu yüzden test yalnızca "binary var mı"ya
# bakmaz, gerçekten bir .idl dosyasını C++'a çevirebiliyor mu diye de bakar.
# Java bağımlılığı eksikse bu adım burada patlar.
#
# Kullanım:  bash tools/verify_fastddsgen.sh

set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck disable=SC1091
source "$SCRIPT_DIR/fastdds_env.sh"

error_count=0
ok() { echo "  [OK]   $1"; }
fail() { echo "  [HATA] $1"; error_count=$((error_count + 1)); }

echo "== Faz 0.2: fastddsgen doğrulaması =="

# --- 1) Java var mı? ---------------------------------------------------------
if java -version >/dev/null 2>&1; then
    ok "java bulundu: $(java -version 2>&1 | head -1)"
else
    fail "java bulunamadı (openjdk-17-jdk kurulu mu?)"
    echo "SONUC: BASARISIZ"; exit 1
fi

# --- 2) fastddsgen PATH'te mi ve sürüm veriyor mu? ---------------------------
if ! command -v fastddsgen >/dev/null 2>&1; then
    fail "fastddsgen PATH'te bulunamadı"
    echo "SONUC: BASARISIZ"; exit 1
fi
# Not: `fastddsgen -version` önce JVM'in kendi sürüm satırlarını basar;
# bizi ilgilendiren satır "fastddsgen version X.Y.Z" olanı.
version="$(fastddsgen -version 2>&1 | grep -i "^fastddsgen version" | head -1)"
if [ -n "$version" ]; then
    ok "fastddsgen çalışıyor: $version"
else
    fail "fastddsgen -version çıktı vermedi"
fi

# --- 3) Gerçekten IDL derleyebiliyor mu? -------------------------------------
# `mktemp -d`: geçici bir dizin oluşturur. Testi projenin içine kirletmemek için.
TEMP_DIR="$(mktemp -d)"
trap 'rm -rf "$GECICI"' EXIT

cat > "$TEMP_DIR/Sonda.idl" <<'IDL'
// fastddsgen'in çalıştığını kanıtlamak için tek seferlik minik bir IDL.
struct Sonda
{
    unsigned long value;
    string text;
};
IDL

if fastddsgen -replace -d "$TEMP_DIR" "$TEMP_DIR/Sonda.idl" >"$TEMP_DIR/gen.log" 2>&1; then
    ok "örnek IDL derlendi"
else
    fail "fastddsgen IDL derleyemedi:"
    sed 's/^/         /' "$TEMP_DIR/gen.log"
fi

# Üretilmesi beklenen dosyalar (Fast DDS 3.x tip desteği)
for expected in SondaPubSubTypes.cxx SondaPubSubTypes.hpp Sonda.hpp; do
    if [ -f "$TEMP_DIR/$expected" ]; then
        ok "üretildi: $expected"
    else
        fail "üretilmedi: $expected"
    fi
done

echo
if [ "$error_count" -eq 0 ]; then
    echo "SONUC: BASARILI - fastddsgen kullanıma hazır"
    exit 0
fi
echo "SONUC: BASARISIZ - $error_count kontrol başarısız"
exit 1
