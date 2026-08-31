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
#    B) `docker restart` ile yeniden başlatma. Kesinti süresi host'un hızına
#       göre 3 saniyenin altında da üstünde de kalabilir; yani A'daki
#       OFFLINE -> ONLINE yolu ya devreye girer ya girmez. Test her iki
#       durumda da geçerli olan asıl gereksinimi doğrular: taze telemetri
#       KABUL edilmeli.
#
#       Kesintinin 3 saniyenin ALTINDA kaldığı saf "hızlı restart" durumu —
#       peer'ın hiç OFFLINE görünmediği, dolayısıyla yalnızca büyük geri
#       sıçrama tespitinin (PeerManager::RESTART_DETECTION_THRESHOLD) kurtardığı
#       durum — konteyner başlatma süresine bağlı olmadan deterministik
#       şekilde birim testlerinde sınanıyor (test_peer_manager.cpp,
#       "HizliRestartBuyukGeriSicramaylaTespitEdilir").
#
#  Kullanım:  bash tests/integration/test_05_seq_sifirlama.sh
# =============================================================================

set -uo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/common.sh"

echo "== Faz 6.5: Restart sonrasi seq sifirlama =="
cleanup_on_exit
stop_swarm
start_swarm

section "Ilk telemetri akisi basliyor"
verify_log drone_scout "\[peer\] yeni peer: id=2" 60 "Gözcü, Müdahale-1'i tanıdı"
verify_log drone_scout "\[telemetry\] id=2 akisi basladi" 40 \
    "Gözcü, Müdahale-1'in telemetri akışını almaya başladı"

# Sayaç ilerlesin: restart sonrası gelen düşük seq'in reddedilmemesi
# asıl sınanan şey. 10 Hz'de 8 saniye ~80 paket eder.
echo "-- telemetri sayacinin ilerlemesi icin 8 saniye bekleniyor"
sleep 8

# ---------------------------------------------------------------------------
#  A) Yavaş restart — peer OFFLINE'a düşüyor
# ---------------------------------------------------------------------------
section "A) Yavas restart (peer OFFLINE'a dusuyor)"

previous_stream_count="$(count_log drone_scout '\[telemetry\] id=2 akisi basladi')"
echo "     'akisi basladi' satiri (restart oncesi): $previous_stream_count"

echo "-- docker stop: drone_striker_1"
compose stop -t 0 drone_striker_1 >/dev/null 2>&1

verify_log drone_scout "\[peer\] kayip peer tespit edildi" 30 \
    "Gözcü, Müdahale-1'in kaybolduğunu fark etti (OFFLINE)"

echo "-- docker start: drone_striker_1"
compose start drone_striker_1 >/dev/null 2>&1

verify_log drone_scout "\[peer\] geri dondu: id=2 \(seq takibi sifirlandi\)" 45 \
    "OFFLINE -> ONLINE geçişinde seq takibi sıfırlandı"

# Sayaç sıfırlanmasaydı restart sonrası gelen düşük seq'ler "bayat" diye
# reddedilir ve bu satır bir daha hiç görünmezdi.
# DİKKAT: wait_for_log burada YANLIŞ araç olurdu — desen zaten var (restart
# öncesinden) ve anında başarılı döner. "Bir kez daha yazılmalı" demek için
# sayacın artmasını beklemek gerekiyor.
if wait_for_count_increase drone_scout "\[telemetry\] id=2 akisi basladi" "$previous_stream_count" 45; then
    echo "     'akisi basladi' satiri (restart sonrasi): $(count_log drone_scout '\[telemetry\] id=2 akisi basladi')"
    ok "Taze telemetri KABUL edildi (akış yeniden başladı)"
else
    fail "Restart sonrası yeni telemetri akışı görülmedi - taze veri 'bayat' diye reddedilmiş olabilir"
fi

verify_log drone_striker_1 "\[peer\] yeni peer: id=1" 45 \
    "Yeniden başlayan düğüm de sürüyü yeniden keşfetti"

# ---------------------------------------------------------------------------
#  B) Hızlı restart — peer OFFLINE'a hiç düşmüyor
# ---------------------------------------------------------------------------
section "B) docker restart ile yeniden baslatma"

# Sayacin restart tespit esigini (20 paket) RAHATCA asmasi gerekiyor.
# 10 Hz'de 12 saniye ~120 paket eder; esigin 6 katindan fazla pay birakiyoruz
# ki test zamanlama sansina bagli olmasin.
echo "-- sayacin restart tespit esigini asmasi icin 12 saniye bekleniyor"
sleep 12

previous_stream_count="$(count_log drone_scout '\[telemetry\] id=2 akisi basladi')"
previous_lost_count="$(count_log drone_scout '\[peer\] kayip peer tespit edildi')"
echo "     'akisi basladi' satiri (hizli restart oncesi): $previous_stream_count"

echo "-- docker restart: drone_striker_1"
compose restart -t 0 drone_striker_1 >/dev/null 2>&1

if wait_for_count_increase drone_scout "\[telemetry\] id=2 akisi basladi" "$previous_stream_count" 60; then
    echo "     'akisi basladi' satiri (restart sonrasi): $(count_log drone_scout '\[telemetry\] id=2 akisi basladi')"
    ok "Restart sonrası taze telemetri KABUL edildi"
else
    fail "Restart sonrası taze telemetri reddedildi - sayaç sıfırlanmamış"
fi

# Hangi yolun devreye girdiğini bilgi olarak raporluyoruz (ikisi de geçerli).
if [ "$(count_log drone_scout '\[peer\] kayip peer tespit edildi')" -gt "$previous_lost_count" ]; then
    echo "     yol: kesinti 3 sn'yi asti -> OFFLINE -> ONLINE sifirlamasi"
else
    echo "     yol: kesinti 3 sn'nin altinda kaldi -> buyuk geri sicrama tespiti"
fi

report_result
