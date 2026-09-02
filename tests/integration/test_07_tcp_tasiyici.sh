#!/usr/bin/env bash
# =============================================================================
#  Faz 7 — TCP taşıyıcısının uçtan uca doğrulaması (Bölüm 3.4)
#
#  Doğrulanan iddia:
#    task_alloc ve consensus topic'leri GERÇEK bir TCP soketi üzerinden akar;
#    heartbeat ve telemetri ise UDP'de kalır. Keşif (SPDP) hâlâ UDP multicast
#    ile otomatik yürür — hiçbir düğüm bir başkasının IP'sini önceden bilmez.
#
#  İŞ BÖLÜMÜ — hangi iddia nerede kanıtlanıyor:
#    * "task_alloc/consensus YALNIZCA TCP locator ilan eder" (yani taşıyıcı
#      ayrımının kendisi) birim testinde kanıtlanır:
#          FastDDSTcp.GuvenilirTopicYalnizcaTcpLocatorIlanEder
#    * "TCP taşıyıcısı gerçek bir ağda, ayrı makinelerde, gerçekten ayağa
#      kalkıyor ve düğümler birbirine bağlanıyor" ise ancak burada görülebilir.
#      Aynı süreçte iki wrapper kurulduğunda Fast DDS'in "intraprocess
#      delivery" özelliği taşıma katmanını tümüyle atlar; gerçek soketler
#      yalnızca ayrı süreçlerde (burada: ayrı container'larda) kurulur.
#
#  ÖLÇÜM YÖNTEMİ: runtime imajında `ss`/`netstat` yok (bilinçli olarak minimal
#  tutuluyor). Bunun yerine çekirdeğin her container'ın kendi ağ isim
#  uzayında (network namespace) tuttuğu sayaçlar okunuyor:
#      /proc/net/tcp   -> hangi portlar dinleniyor, hangi bağlantılar kurulu
#      /proc/net/snmp  -> TCP segment ve UDP datagram sayaçları
#
#  Kullanım:  bash tests/integration/test_07_tcp_tasiyici.sh
# =============================================================================

set -uo pipefail
source "$(dirname "${BASH_SOURCE[0]}")/common.sh"

# docker-compose'daki TCP_PORT varsayılanı. /proc/net/tcp portları ONALTILIK
# (hex) yazdığı için karşılığını da hesaplıyoruz: 5100 -> 13EC
TCP_PORT=5100
TCP_PORT_HEX="$(printf '%04X' "$TCP_PORT")"

SERVICES="gcs_node drone_scout drone_striker_1 drone_striker_2"

# --- /proc yardımcıları ------------------------------------------------------

# Container içinde bir dosyayı okur.
read_in_container() {
    compose exec -T "$1" cat "$2" 2>/dev/null
}

# Verilen serviste TCP_PORT'u DİNLEYEN bir soket var mı?
# /proc/net/tcp sütunları: sl local_address rem_address st ...
# st == 0A  ->  LISTEN
listening_on_tcp_port() {
    read_in_container "$1" /proc/net/tcp \
        | awk -v port="$TCP_PORT_HEX" '$2 ~ ":"port"$" && $4 == "0A" { found = 1 }
                                       END { exit !found }'
}

# Verilen serviste, KARŞI tarafın portu TCP_PORT olan KURULU (ESTABLISHED)
# bağlantı sayısı. st == 01 -> ESTABLISHED
#
# Karşı tarafa göre sayıyoruz (rem_address), çünkü bu düğüm hem sunucu
# (kendi 5100'ünü dinler) hem istemci (karşının 5100'üne bağlanır) olabilir;
# ikisini birden saymak aynı bağlantıyı iki kez saymaya yol açardı.
established_outgoing_count() {
    read_in_container "$1" /proc/net/tcp \
        | awk -v port="$TCP_PORT_HEX" '$3 ~ ":"port"$" && $4 == "01" { n++ }
                                       END { print n + 0 }'
}

# Kurulu bağlantıların karşı IP'lerini okunur biçimde döker.
# /proc/net/tcp adresi little-endian hex tutar: 0B0014AC -> 172.20.0.11
established_peer_ips() {
    read_in_container "$1" /proc/net/tcp \
        | awk -v port="$TCP_PORT_HEX" '$3 ~ ":"port"$" && $4 == "01" { print $3 }' \
        | cut -d: -f1 \
        | while read -r hex_address; do
              printf '%d.%d.%d.%d\n' \
                  "0x${hex_address:6:2}" "0x${hex_address:4:2}" \
                  "0x${hex_address:2:2}" "0x${hex_address:0:2}"
          done
}

# /proc/net/snmp'den tek bir sayaç okur.
#   snmp_counter <servis> <protokol> <alan>     ör: snmp_counter gcs_node Tcp OutSegs
#
# Dosya biçimi iki satır halindedir: önce başlık satırı (alan adları), hemen
# altında değer satırı. Alanın kaçıncı sütun olduğunu başlıktan buluyoruz.
snmp_counter() {
    local service="$1" protocol="$2" field="$3"
    read_in_container "$service" /proc/net/snmp \
        | awk -v proto="$protocol:" -v field="$field" '
            $1 == proto && header == "" { for (i = 2; i <= NF; i++) if ($i == field) column = i
                                          header = 1
                                          next }
            $1 == proto && header != "" { print $column + 0; exit }'
}

# =============================================================================

echo "== Faz 7: TCP taşıyıcısı entegrasyon testi =="
cleanup_on_exit
stop_swarm
start_swarm

section "Sürü hazır"
verify_log gcs_node "yeni peer: id=3" 60 "GCS üç drone'u da keşfetti (UDP multicast keşfi çalışıyor)"

# --------------------------------------------------------------------------
section "Her düğüm TCP portunu dinliyor"
# --------------------------------------------------------------------------
for service in $SERVICES; do
    if listening_on_tcp_port "$service"; then
        ok "$service TCP $TCP_PORT portunu dinliyor"
    else
        fail "$service TCP $TCP_PORT portunu DİNLEMİYOR"
    fi
done

# --------------------------------------------------------------------------
section "Düğümler birbirine TCP ile bağlanmış"
# --------------------------------------------------------------------------
# Her düğüm task_alloc + consensus için diğer üç düğümle konuşur. Bağlantının
# hangi yönde kurulduğu (kim istemci, kim sunucu) Fast DDS'in iç kararıdır;
# bu yüzden "en az bir kurulu bağlantı" arıyoruz, sabit bir sayı değil.
total_established=0
for service in $SERVICES; do
    count="$(established_outgoing_count "$service")"
    total_established=$((total_established + count))
    echo "     $service -> $count kurulu TCP bağlantısı"
done

if [ "$total_established" -gt 0 ]; then
    ok "Sürüde toplam $total_established kurulu TCP bağlantısı var"
else
    fail "Hiç kurulu TCP bağlantısı yok — TCP taşıyıcısı devrede değil"
fi

# Bağlantıların gerçekten sürü ağındaki düğümlere gittiğini doğruluyoruz;
# rastgele bir yerel bağlantıyı TCP taşıyıcısı sanmayalım.
peer_ips="$(for service in $SERVICES; do established_peer_ips "$service"; done | sort -u)"
echo "     bağlanılan adresler: $(echo "$peer_ips" | tr '\n' ' ')"

if echo "$peer_ips" | grep -qE '^172\.20\.0\.(10|11|12|13)$'; then
    ok "TCP bağlantıları sürü ağındaki düğüm IP'lerine gidiyor"
else
    fail "TCP bağlantıları sürü düğümlerine gitmiyor"
fi

# --------------------------------------------------------------------------
section "TCP üzerinde gerçek trafik var"
# --------------------------------------------------------------------------
# ÖLÇÜNÜN SINIRI: /proc/net/snmp'deki TCP OutSegs saf ACK segmentlerini de
# sayar ve keşif trafiği de TCP kullanır. Bu yüzden "şu topic TCP'den aktı"
# iddiası burada segment sayısıyla KANITLANAMAZ — o iddia, endpoint'lerin
# ilan ettiği locator'ları doğrudan okuyan birim testinde kanıtlanıyor:
#     FastDDSTcp.GuvenilirTopicYalnizcaTcpLocatorIlanEder
#
# Burada ölçülen daha mütevazı ama yine de gerekli bir şey: TCP kanalı ölü
# değil, sürü çalışırken üzerinden gerçekten trafik geçiyor.
tcp_before="$(snmp_counter gcs_node Tcp OutSegs)"
udp_before="$(snmp_counter gcs_node Udp OutDatagrams)"
echo "     ölçüm başlangıcı: TCP OutSegs=$tcp_before  UDP OutDatagrams=$udp_before"

verify_log gcs_node "\[consensus\] sonuc tx=1 COMMITTED" 60 "Birinci oylama COMMITTED"
verify_log gcs_node "\[gcs\] gorev emri yayinlandi task_id=1" 30 "Birinci görev emri yayınlandı"
verify_log gcs_node "\[consensus\] sonuc tx=2 COMMITTED" 60 "İkinci oylama COMMITTED"
verify_log gcs_node "\[gcs\] gorev emri yayinlandi task_id=2" 30 "İkinci görev emri yayınlandı"

tcp_after="$(snmp_counter gcs_node Tcp OutSegs)"
udp_after="$(snmp_counter gcs_node Udp OutDatagrams)"

tcp_delta=$((tcp_after - tcp_before))
udp_delta=$((udp_after - udp_before))
echo "     consensus turu boyunca: TCP +$tcp_delta segment,  UDP +$udp_delta datagram"

if [ "$tcp_delta" -gt 0 ]; then
    ok "Consensus turu boyunca TCP kanalından trafik geçti (+$tcp_delta segment)"
else
    fail "TCP kanalı ölü — hiç segment gönderilmedi"
fi

if [ "$udp_delta" -gt 0 ]; then
    ok "UDP kanalı da akıyor (+$udp_delta datagram — heartbeat/telemetri)"
else
    fail "UDP kanalından hiç datagram geçmedi"
fi

# --------------------------------------------------------------------------
section "İşlevsellik korundu"
# --------------------------------------------------------------------------
# Taşıyıcı değişikliği mevcut davranışı bozmamalı: emirler görevlere dönüşmeli.
verify_log drone_scout "\[task\] gecis: IDLE -> SCOUT_SEARCH" 40 "Gözcü emri alıp arama görevine geçti"
verify_log drone_striker_1 "\[task\] gecis: IDLE -> GO_TO_TARGET" 40 "Müdahale-1 hedefe gitme görevine geçti"
verify_log_absent gcs_node "ABORTED" 1 "Hiçbir oylama ABORTED olmadı"

report_result
result=$?

if [ "$result" -ne 0 ]; then
    summarize_logs
fi

exit "$result"
