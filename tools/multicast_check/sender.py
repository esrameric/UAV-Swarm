"""Faz 0.5 — Multicast GÖNDERİCİ sondası (bkz. receiver.py)."""

import socket
import time

GROUP = "239.255.0.1"
PORT = 7400
TEKRAR = 20

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP)
# TTL (Time To Live) = paketin kaç yönlendiriciden geçebileceği. Aynı bridge
# ağı içinde 1 yeterli; güvenli tarafta kalmak için 2 veriyoruz.
sock.setsockopt(socket.IPPROTO_IP, socket.IP_MULTICAST_TTL, 2)

print(f"GONDERICI: {GROUP}:{PORT} adresine {TEKRAR} paket yollaniyor", flush=True)
for sira in range(TEKRAR):
    sock.sendto(f"SWARM_MULTICAST_PROBE_{sira}".encode(), (GROUP, PORT))
    time.sleep(0.5)
print("GONDERICI: bitti", flush=True)
