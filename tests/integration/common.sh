#!/usr/bin/env bash
# =============================================================================
#  Entegrasyon testleri için ortak yardımcılar
#
#  Bu dosya tek başına çalıştırılmaz; her test script'i başında
#  `source` eder.
#
#  Testlerin tamamı docker-compose ile 4 container ayağa kaldırır ve
#  container'ların LOG ÇIKTISINI otomatik doğrular (Bölüm 0). Elle
#  gözlemlenen bir prosedür yoktur.
# =============================================================================

# Docker komutu. Kullanıcı `docker` grubunda değilse:
#     DOCKER="sudo docker" bash tests/integration/test_...sh
DOCKER="${DOCKER:-docker}"

# Depo kökü (bu dosya tests/integration/ altında).
DEPO_KOKU="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
COMPOSE_DOSYASI="$DEPO_KOKU/docker/docker-compose.yml"

# Testler birbirinin ayağına basmasın diye ortak proje adı.
COMPOSE_PROJE="swarmtest"

# --- Çıktı biçimi ------------------------------------------------------------

basarili_sayisi=0
basarisiz_sayisi=0

gecti() {
    echo "  [GECTI]  $1"
    basarili_sayisi=$((basarili_sayisi + 1))
}

kaldi() {
    echo "  [KALDI]  $1"
    basarisiz_sayisi=$((basarisiz_sayisi + 1))
}

baslik() {
    echo
    echo "== $1 =="
}

# Test sonucunu özetler ve uygun çıkış koduyla döner.
sonucu_bildir() {
    echo
    echo "  gecen: $basarili_sayisi   kalan: $basarisiz_sayisi"
    if [ "$basarisiz_sayisi" -eq 0 ]; then
        echo "SONUC: BASARILI"
        return 0
    fi
    echo "SONUC: BASARISIZ"
    return 1
}

# --- docker compose sarmalayıcıları ------------------------------------------

# Ek compose dosyaları (senaryoya özel ezmeler) EK_COMPOSE dizisiyle verilir.
compose() {
    local dosyalar=(-f "$COMPOSE_DOSYASI")
    local ek
    for ek in "${EK_COMPOSE[@]:-}"; do
        [ -n "$ek" ] && dosyalar+=(-f "$ek")
    done
    $DOCKER compose -p "$COMPOSE_PROJE" "${dosyalar[@]}" "$@"
}

suruyu_baslat() {
    echo "-- sürü başlatılıyor (4 container)"
    compose up -d --no-build >/dev/null 2>&1
}

suruyu_durdur() {
    echo "-- sürü durduruluyor"
    # -v: compose'un oluşturduğu ağ/volume da silinsin, sonraki test temiz
    # bir ortamda başlasın.
    compose down -v --remove-orphans >/dev/null 2>&1
}

# Test hangi sebeple biterse bitsin container'lar arkada kalmasın.
temizlikle_bitir() {
    trap suruyu_durdur EXIT
}

# --- Log yardımcıları --------------------------------------------------------

# Bir servisin o ana kadarki tüm log çıktısı.
servis_logu() {
    compose logs --no-color "$1" 2>/dev/null
}

# Tüm servislerin logu.
tum_loglar() {
    compose logs --no-color 2>/dev/null
}

# Belirtilen desen logda görünene kadar bekler.
#   bekle_log <servis|TUMU> <desen> <azami_saniye>
# Dönüş: desen bulunduysa 0.
bekle_log() {
    local servis="$1"
    local desen="$2"
    local azami="$3"
    local gecen=0

    while [ "$gecen" -lt "$azami" ]; do
        local cikti
        if [ "$servis" = "TUMU" ]; then
            cikti="$(tum_loglar)"
        else
            cikti="$(servis_logu "$servis")"
        fi

        if echo "$cikti" | grep -qE "$desen"; then
            return 0
        fi
        sleep 1
        gecen=$((gecen + 1))
    done
    return 1
}

# bekle_log + sonucu raporla
#   dogrula_log <servis> <desen> <azami_saniye> <aciklama>
dogrula_log() {
    local servis="$1" desen="$2" azami="$3" aciklama="$4"
    if bekle_log "$servis" "$desen" "$azami"; then
        gecti "$aciklama"
    else
        kaldi "$aciklama  (aranan: /$desen/ - $servis)"
    fi
}

# Desenin logda BULUNMAMASI gerektiğini doğrular (bekleme süresi kadar
# bekleyip sonuca bakar).
#   dogrula_log_yok <servis> <desen> <bekleme_saniye> <aciklama>
dogrula_log_yok() {
    local servis="$1" desen="$2" bekleme="$3" aciklama="$4"
    sleep "$bekleme"

    local cikti
    if [ "$servis" = "TUMU" ]; then
        cikti="$(tum_loglar)"
    else
        cikti="$(servis_logu "$servis")"
    fi

    if echo "$cikti" | grep -qE "$desen"; then
        kaldi "$aciklama  (beklenmeyen satir bulundu: /$desen/)"
    else
        gecti "$aciklama"
    fi
}

# Bir desenin SAYISI verilen değerin üstüne çıkana kadar bekler.
#   bekle_sayac_artsin <servis> <desen> <onceki_sayi> <azami_saniye>
#
# Neden ayrı bir yardımcı: bekle_log() deseni ZATEN varsa anında döner.
# "Bu satır bir kez daha yazılmalı" demek istediğimizde bekle_log işe
# yaramaz — eski satırı bulup hemen başarılı sayar ve yenisi için hiç
# beklemez. (Bu, Faz 6.5 testinin düzensiz düşmesinin sebebiydi.)
bekle_sayac_artsin() {
    local servis="$1" desen="$2" onceki="$3" azami="$4"
    local gecen=0

    while [ "$gecen" -lt "$azami" ]; do
        if [ "$(log_say "$servis" "$desen")" -gt "$onceki" ]; then
            return 0
        fi
        sleep 1
        gecen=$((gecen + 1))
    done
    return 1
}

# Bir desenin logda kaç kez geçtiğini sayar.
log_say() {
    local servis="$1" desen="$2"
    local cikti
    if [ "$servis" = "TUMU" ]; then
        cikti="$(tum_loglar)"
    else
        cikti="$(servis_logu "$servis")"
    fi
    echo "$cikti" | grep -cE "$desen"
}

# Hata ayıklamaya yardımcı: testin sonunda logların özetini basar.
loglari_ozetle() {
    echo
    echo "----- container loglari (son 15 satir/servis) -----"
    local servis
    for servis in gcs_node drone_scout drone_striker_1 drone_striker_2; do
        echo "--- $servis ---"
        servis_logu "$servis" | tail -15 | sed 's/^/    /'
    done
    echo "---------------------------------------------------"
}
