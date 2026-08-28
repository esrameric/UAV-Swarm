#!/usr/bin/env bash
# =============================================================================
#  Faz 6.5 — Restart sonrası seq_num / last_seen_seq sıfırlama davranışı
#
#  Neden önemli (Bölüm 3.5): her drone telemetri için hiç sıfırlanmayan bir
#  uint32_t sayaç tutar. Drone yeniden başlayınca süreç sıfırdan başlar,
#  dolayısıyla sayaç da 0'dan başlar. Alıcı tarafta eski (yüksek) seq değeri
#  saklı kalırsa restart sonrası gelen TAZE veri "bayat" diye reddedilir ve
#  o drone bir daha hiç görünmez.
#
#  İKİ AYRI YOL SINANIYOR:
#
#    A) YAVAŞ RESTART — düğüm heartbeat zaman aşımından (3 sn) uzun süre
#       kapalı kalır. Peer OFFLINE'a düşer; geri döndüğünde OFFLINE -> ONLINE
#       geçişi last_seen_seq'i sıfırlar. Planın (Bölüm 3.5) tanımladığı
#       mekanizma budur.
#
#    B) HIZLI RESTART — düğüm 3 saniyeden kısa sürede geri gelir; peer HİÇ
#       OFFLINE görünmez, dolayısıyla A'daki sıfırlama hiç tetiklenmez.
#       Bu durum planda ele alınmamıştı ve gerçek bir açıktı: taze veri
#       sessizce reddediliyordu. Büyük geri sıçrama tespiti (bkz.
#       PeerManager::RESTART_TESPIT_ESIGI) bu boşluğu kapatıyor.
#
#  Kullanım:  bash tests/integration/test_05_seq_sifirlama.sh
# =============================================================================

set -uo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/common.sh"

echo "== Faz 6.5: Restart sonrasi seq sifirlama =="
temizlikle_bitir
suruyu_durdur
suruyu_baslat

baslik "Ilk telemetri akisi basliyor"
dogrula_log drone_scout "\[peer\] yeni peer: id=2" 60 "Gözcü, Müdahale-1'i tanıdı"
dogrula_log drone_scout "\[telemetry\] id=2 akisi basladi" 40 \
    "Gözcü, Müdahale-1'in telemetri akışını almaya başladı"

# Sayaç ilerlesin: restart sonrası gelen düşük seq'in reddedilmemesi
# asıl sınanan şey. 10 Hz'de 8 saniye ~80 paket eder.
echo "-- telemetri sayacinin ilerlemesi icin 8 saniye bekleniyor"
sleep 8

# ---------------------------------------------------------------------------
#  A) Yavaş restart — peer OFFLINE'a düşüyor
# ---------------------------------------------------------------------------
baslik "A) Yavas restart (peer OFFLINE'a dusuyor)"

onceki_akis="$(log_say drone_scout '\[telemetry\] id=2 akisi basladi')"
echo "     'akisi basladi' satiri (restart oncesi): $onceki_akis"

echo "-- docker stop: drone_striker_1"
compose stop -t 0 drone_striker_1 >/dev/null 2>&1

dogrula_log drone_scout "\[peer\] kayip peer tespit edildi" 30 \
    "Gözcü, Müdahale-1'in kaybolduğunu fark etti (OFFLINE)"

echo "-- docker start: drone_striker_1"
compose start drone_striker_1 >/dev/null 2>&1

dogrula_log drone_scout "\[peer\] geri dondu: id=2 \(seq takibi sifirlandi\)" 45 \
    "OFFLINE -> ONLINE geçişinde seq takibi sıfırlandı"

# Sayaç sıfırlanmasaydı restart sonrası gelen düşük seq'ler "bayat" diye
# reddedilir ve bu satır bir daha hiç görünmezdi.
sonraki_akis_beklenen=$((onceki_akis + 1))
if bekle_log drone_scout "\[telemetry\] id=2 akisi basladi" 45; then
    yeni_akis="$(log_say drone_scout '\[telemetry\] id=2 akisi basladi')"
    echo "     'akisi basladi' satiri (restart sonrasi): $yeni_akis"
    if [ "$yeni_akis" -ge "$sonraki_akis_beklenen" ]; then
        gecti "Taze telemetri KABUL edildi (akış yeniden başladı)"
    else
        kaldi "Restart sonrası yeni telemetri akışı görülmedi - taze veri 'bayat' diye reddedilmiş olabilir"
    fi
else
    kaldi "Restart sonrası telemetri akışı hiç başlamadı"
fi

dogrula_log drone_striker_1 "\[peer\] yeni peer: id=1" 45 \
    "Yeniden başlayan düğüm de sürüyü yeniden keşfetti"

# ---------------------------------------------------------------------------
#  B) Hızlı restart — peer OFFLINE'a hiç düşmüyor
# ---------------------------------------------------------------------------
baslik "B) Hizli restart (peer OFFLINE'a hic dusmuyor)"

echo "-- sayacin yeniden ilerlemesi icin 8 saniye bekleniyor"
sleep 8

onceki_akis="$(log_say drone_scout '\[telemetry\] id=2 akisi basladi')"
onceki_kayip="$(log_say drone_scout '\[peer\] kayip peer tespit edildi')"
echo "     'akisi basladi' satiri (hizli restart oncesi): $onceki_akis"

echo "-- docker restart: drone_striker_1 (3 saniyeden kisa kesinti)"
compose restart -t 0 drone_striker_1 >/dev/null 2>&1

if bekle_log drone_scout "\[telemetry\] id=2 akisi basladi" 45 && \
   [ "$(log_say drone_scout '\[telemetry\] id=2 akisi basladi')" -gt "$onceki_akis" ]; then
    yeni_akis="$(log_say drone_scout '\[telemetry\] id=2 akisi basladi')"
    echo "     'akisi basladi' satiri (hizli restart sonrasi): $yeni_akis"
    gecti "Hızlı restart sonrası taze telemetri KABUL edildi (büyük geri sıçrama tespiti)"
else
    kaldi "Hızlı restart sonrası taze telemetri reddedildi - sayaç sıfırlanmamış"
fi

sonucu_bildir
