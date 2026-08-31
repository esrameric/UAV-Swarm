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
REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
COMPOSE_FILE="$REPO_ROOT/docker/docker-compose.yml"

# Testler birbirinin ayağına basmasın diye ortak proje adı.
COMPOSE_PROJECT="swarmtest"

# --- Çıktı biçimi ------------------------------------------------------------

pass_count=0
fail_count=0

ok() {
    echo "  [GECTI]  $1"
    pass_count=$((pass_count + 1))
}

fail() {
    echo "  [KALDI]  $1"
    fail_count=$((fail_count + 1))
}

section() {
    echo
    echo "== $1 =="
}

# Test sonucunu özetler ve uygun çıkış koduyla döner.
report_result() {
    echo
    echo "  gecen: $pass_count   kalan: $fail_count"
    if [ "$fail_count" -eq 0 ]; then
        echo "SONUC: BASARILI"
        return 0
    fi
    echo "SONUC: BASARISIZ"
    return 1
}

# --- docker compose wrapper'ları ------------------------------------------

# Ek compose dosyaları (senaryoya özel ezmeler) EK_COMPOSE dizisiyle verilir.
compose() {
    local files=(-f "$COMPOSE_FILE")
    local extra
    for extra in "${EK_COMPOSE[@]:-}"; do
        [ -n "$extra" ] && files+=(-f "$extra")
    done
    $DOCKER compose -p "$COMPOSE_PROJECT" "${files[@]}" "$@"
}

start_swarm() {
    echo "-- sürü başlatılıyor (4 container)"
    compose up -d --no-build >/dev/null 2>&1
}

stop_swarm() {
    echo "-- sürü durduruluyor"
    # -v: compose'un oluşturduğu ağ/volume da silinsin, sonraki test temiz
    # bir ortamda başlasın.
    compose down -v --remove-orphans >/dev/null 2>&1
}

# Test hangi sebeple biterse bitsin container'lar arkada kalmasın.
cleanup_on_exit() {
    trap stop_swarm EXIT
}

# --- Log yardımcıları --------------------------------------------------------

# Bir servisin o ana kadarki tüm log çıktısı.
service_log() {
    compose logs --no-color "$1" 2>/dev/null
}

# Tüm servislerin logu.
all_logs() {
    compose logs --no-color 2>/dev/null
}

# Belirtilen desen logda görünene kadar bekler.
#   wait_for_log <servis|ALL> <desen> <azami_saniye>
# Dönüş: desen bulunduysa 0.
wait_for_log() {
    local service="$1"
    local pattern="$2"
    local max="$3"
    local elapsed=0

    while [ "$elapsed" -lt "$max" ]; do
        local output
        if [ "$service" = "ALL" ]; then
            output="$(all_logs)"
        else
            output="$(service_log "$service")"
        fi

        if echo "$output" | grep -qE "$pattern"; then
            return 0
        fi
        sleep 1
        elapsed=$((elapsed + 1))
    done
    return 1
}

# wait_for_log + sonucu raporla
#   verify_log <servis> <desen> <azami_saniye> <aciklama>
verify_log() {
    local service="$1" pattern="$2" max="$3" description="$4"
    if wait_for_log "$service" "$pattern" "$max"; then
        ok "$description"
    else
        fail "$description  (aranan: /$pattern/ - $service)"
    fi
}

# Desenin logda BULUNMAMASI gerektiğini doğrular (bekleme süresi kadar
# bekleyip sonuca bakar).
#   verify_log_absent <servis> <desen> <bekleme_saniye> <aciklama>
verify_log_absent() {
    local service="$1" pattern="$2" wait_seconds="$3" description="$4"
    sleep "$wait_seconds"

    local output
    if [ "$service" = "ALL" ]; then
        output="$(all_logs)"
    else
        output="$(service_log "$service")"
    fi

    if echo "$output" | grep -qE "$pattern"; then
        fail "$description  (beklenmeyen satir bulundu: /$pattern/)"
    else
        ok "$description"
    fi
}

# Bir desenin SAYISI verilen değerin üstüne çıkana kadar bekler.
#   wait_for_count_increase <servis> <desen> <onceki_sayi> <azami_saniye>
#
# Neden ayrı bir yardımcı: wait_for_log() deseni ZATEN varsa anında döner.
# "Bu satır bir kez daha yazılmalı" demek istediğimizde wait_for_log işe
# yaramaz — eski satırı bulup hemen başarılı sayar ve yenisi için hiç
# beklemez. (Bu, Faz 6.5 testinin düzensiz düşmesinin sebebiydi.)
wait_for_count_increase() {
    local service="$1" pattern="$2" previous="$3" max="$4"
    local elapsed=0

    while [ "$elapsed" -lt "$max" ]; do
        if [ "$(count_log "$service" "$pattern")" -gt "$previous" ]; then
            return 0
        fi
        sleep 1
        elapsed=$((elapsed + 1))
    done
    return 1
}

# Bir desenin logda kaç kez geçtiğini sayar.
count_log() {
    local service="$1" pattern="$2"
    local output
    if [ "$service" = "ALL" ]; then
        output="$(all_logs)"
    else
        output="$(service_log "$service")"
    fi
    echo "$output" | grep -cE "$pattern"
}

# Hata ayıklamaya yardımcı: testin sonunda logların özetini basar.
summarize_logs() {
    echo
    echo "----- container loglari (son 15 satir/servis) -----"
    local service
    for service in gcs_node drone_scout drone_striker_1 drone_striker_2; do
        echo "--- $service ---"
        service_log "$service" | tail -15 | sed 's/^/    /'
    done
    echo "---------------------------------------------------"
}
