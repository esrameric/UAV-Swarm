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

BETIK_DIZINI="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck disable=SC1091
source "$BETIK_DIZINI/fastdds_env.sh"

hata_sayisi=0
basarili() { echo "  [OK]   $1"; }
basarisiz() { echo "  [HATA] $1"; hata_sayisi=$((hata_sayisi + 1)); }

echo "== Faz 0.2: fastddsgen doğrulaması =="

# --- 1) Java var mı? ---------------------------------------------------------
if java -version >/dev/null 2>&1; then
    basarili "java bulundu: $(java -version 2>&1 | head -1)"
else
    basarisiz "java bulunamadı (openjdk-17-jdk kurulu mu?)"
    echo "SONUC: BASARISIZ"; exit 1
fi

# --- 2) fastddsgen PATH'te mi ve sürüm veriyor mu? ---------------------------
if ! command -v fastddsgen >/dev/null 2>&1; then
    basarisiz "fastddsgen PATH'te bulunamadı"
    echo "SONUC: BASARISIZ"; exit 1
fi
# Not: `fastddsgen -version` önce JVM'in kendi sürüm satırlarını basar;
# bizi ilgilendiren satır "fastddsgen version X.Y.Z" olanı.
surum="$(fastddsgen -version 2>&1 | grep -i "^fastddsgen version" | head -1)"
if [ -n "$surum" ]; then
    basarili "fastddsgen çalışıyor: $surum"
else
    basarisiz "fastddsgen -version çıktı vermedi"
fi

# --- 3) Gerçekten IDL derleyebiliyor mu? -------------------------------------
# `mktemp -d`: geçici bir dizin oluşturur. Testi projenin içine kirletmemek için.
GECICI="$(mktemp -d)"
trap 'rm -rf "$GECICI"' EXIT

cat > "$GECICI/Sonda.idl" <<'IDL'
// fastddsgen'in çalıştığını kanıtlamak için tek seferlik minik bir IDL.
struct Sonda
{
    unsigned long deger;
    string metin;
};
IDL

if fastddsgen -replace -d "$GECICI" "$GECICI/Sonda.idl" >"$GECICI/gen.log" 2>&1; then
    basarili "örnek IDL derlendi"
else
    basarisiz "fastddsgen IDL derleyemedi:"
    sed 's/^/         /' "$GECICI/gen.log"
fi

# Üretilmesi beklenen dosyalar (Fast DDS 3.x tip desteği)
for beklenen in SondaPubSubTypes.cxx SondaPubSubTypes.hpp Sonda.hpp; do
    if [ -f "$GECICI/$beklenen" ]; then
        basarili "üretildi: $beklenen"
    else
        basarisiz "üretilmedi: $beklenen"
    fi
done

echo
if [ "$hata_sayisi" -eq 0 ]; then
    echo "SONUC: BASARILI - fastddsgen kullanıma hazır"
    exit 0
fi
echo "SONUC: BASARISIZ - $hata_sayisi kontrol başarısız"
exit 1
