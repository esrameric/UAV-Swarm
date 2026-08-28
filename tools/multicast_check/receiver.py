"""Faz 0.5 — Multicast ALICI sondası.

Bir UDP multicast grubuna katılır ve tek bir paket bekler. Paket gelirse
0, gelmezse 1 koduyla çıkar; böylece kabuk script'i sonucu otomatik
değerlendirebilir.

Not: Bu dosya projenin C++ koduna dahil değildir — yalnızca Docker'ın
custom bridge ağında multicast'in gerçekten çalıştığını doğrulamak için
kullanılan tek seferlik bir ortam testidir (bkz. tools/verify_multicast.sh).
"""

import socket
import struct
import sys

GROUP = "239.255.0.1"  # Fast DDS'in varsayılan SPDP discovery multicast adresi
PORT = 7400            # Fast DDS'in varsayılan discovery portu
TIMEOUT_SANIYE = 25

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP)
sock.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
sock.bind(("", PORT))

# Çekirdeğe "bu multicast grubunu dinlemek istiyorum" diyoruz (IGMP join).
mreq = struct.pack("4sl", socket.inet_aton(GROUP), socket.INADDR_ANY)
sock.setsockopt(socket.IPPROTO_IP, socket.IP_ADD_MEMBERSHIP, mreq)
sock.settimeout(TIMEOUT_SANIYE)

print(f"ALICI: {GROUP}:{PORT} dinleniyor ({TIMEOUT_SANIYE} sn)...", flush=True)

try:
    veri, gonderen = sock.recvfrom(1024)
except socket.timeout:
    print("SONUC: BASARISIZ - multicast paketi alinamadi", flush=True)
    sys.exit(1)

print(f"ALINDI: {veri.decode()} <- {gonderen[0]}", flush=True)
print("SONUC: BASARILI", flush=True)
sys.exit(0)
