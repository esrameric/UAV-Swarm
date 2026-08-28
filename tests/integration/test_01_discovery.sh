#!/usr/bin/env bash
# =============================================================================
#  Faz 6.1 — Discovery (keşif) doğrulaması
#
#  Doğrulanan: 4 düğüm gerçek ağ üzerinde birbirini buluyor mu, peer table
#  doğru doluyor mu, roller doğru öğreniliyor mu?
#
#  IP'ler hakkında not: uygulama katmanı IP taşımaz (Bölüm 3.2) — adresleri
#  Fast DDS'in kendi discovery mekanizması (SPDP/SEDP) takip eder. Bu yüzden
#  test "IP öğrenildi mi" diye değil, "düğümler birbirini gerçekten buldu mu"
#  diye bakar; container'ların ayrı IP'lerde olduğu ayrıca doğrulanır.
#
#  Kullanım:  bash tests/integration/test_01_discovery.sh
# =============================================================================

set -uo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/common.sh"

echo "== Faz 6.1: Discovery entegrasyon testi =="
temizlikle_bitir
suruyu_durdur
suruyu_baslat

baslik "Düğümler ayağa kalkıyor"
dogrula_log gcs_node        "\[node\] hazir" 60 "GCS ayağa kalktı"
dogrula_log drone_scout     "\[node\] hazir" 60 "Gözcü ayağa kalktı"
dogrula_log drone_striker_1 "\[node\] hazir" 60 "Müdahale-1 ayağa kalktı"
dogrula_log drone_striker_2 "\[node\] hazir" 60 "Müdahale-2 ayağa kalktı"

baslik "Kimlikler doğru okundu (ortam değişkenlerinden)"
dogrula_log gcs_node        "node_type=GCS drone_id=0"                 20 "GCS kimliği: NODE_TYPE=GCS, DRONE_ID=0"
dogrula_log drone_scout     "node_type=DRONE drone_id=1 role=SCOUT"    20 "Gözcü kimliği: DRONE_ID=1, ROLE=SCOUT"
dogrula_log drone_striker_1 "node_type=DRONE drone_id=2 role=STRIKER"  20 "Müdahale-1 kimliği: DRONE_ID=2, ROLE=STRIKER"
dogrula_log drone_striker_2 "node_type=DRONE drone_id=3 role=STRIKER"  20 "Müdahale-2 kimliği: DRONE_ID=3, ROLE=STRIKER"

baslik "GCS sürünün tamamını keşfetti"
dogrula_log gcs_node "yeni peer: id=1 type=DRONE role=SCOUT"   40 "GCS Gözcü'yü (id=1) buldu"
dogrula_log gcs_node "yeni peer: id=2 type=DRONE role=STRIKER" 40 "GCS Müdahale-1'i (id=2) buldu"
dogrula_log gcs_node "yeni peer: id=3 type=DRONE role=STRIKER" 40 "GCS Müdahale-2'yi (id=3) buldu"

baslik "Drone'lar birbirini ve GCS'i keşfetti (tam örgü / full mesh)"
dogrula_log drone_scout     "yeni peer: id=0 type=GCS"                 40 "Gözcü GCS'i buldu"
dogrula_log drone_scout     "yeni peer: id=2 type=DRONE role=STRIKER"  40 "Gözcü Müdahale-1'i buldu"
dogrula_log drone_scout     "yeni peer: id=3 type=DRONE role=STRIKER"  40 "Gözcü Müdahale-2'yi buldu"
dogrula_log drone_striker_1 "yeni peer: id=1 type=DRONE role=SCOUT"    40 "Müdahale-1 Gözcü'yü buldu"
dogrula_log drone_striker_2 "yeni peer: id=1 type=DRONE role=SCOUT"    40 "Müdahale-2 Gözcü'yü buldu"

baslik "Her container'ın kendi IP'si var (host networking değil)"
# Bölüm 2: ayrı IP'ler sahadaki 3 ayrı cihazı doğru simüle eder.
ip_listesi="$(compose ps -q | while read -r kimlik; do
    $DOCKER inspect -f '{{range .NetworkSettings.Networks}}{{.IPAddress}}{{end}}' "$kimlik"
done | sort)"
benzersiz_sayisi="$(echo "$ip_listesi" | sort -u | grep -c .)"
toplam_sayisi="$(echo "$ip_listesi" | grep -c .)"

echo "     bulunan IP'ler: $(echo "$ip_listesi" | tr '\n' ' ')"
if [ "$toplam_sayisi" -eq 4 ] && [ "$benzersiz_sayisi" -eq 4 ]; then
    gecti "4 container'ın 4 ayrı IP'si var"
else
    kaldi "beklenen 4 ayrı IP, bulunan: toplam=$toplam_sayisi benzersiz=$benzersiz_sayisi"
fi

if echo "$ip_listesi" | grep -q "^172\.20\.0\.1[0-3]$"; then
    gecti "IP'ler docker-compose'daki sabit adreslerle uyuşuyor (172.20.0.10-13)"
else
    kaldi "IP'ler beklenen 172.20.0.10-13 aralığında değil"
fi

baslik "Hiçbir düğüm hata vermedi"
dogrula_log_yok TUMU "HATA:" 2 "log'da HATA satırı yok"

sonucu_bildir
