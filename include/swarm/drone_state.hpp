// ============================================================================
//  DroneState — bu düğümün KENDİ uçuş durumu
//
//  PeerInfo "başkalarını nasıl görüyorum" bilgisiydi; DroneState ise "ben
//  neredeyim, ne kadar hızlıyım, bataryam ne durumda" bilgisidir. Telemetri
//  mesajı (Bölüm 3.4) doğrudan bu alanlardan doldurulur.
//
//  Gerçek bir uçuş kontrolcüsü (flight controller) olmadığı için (SITL),
//  konum hareket task'ları tarafından adım adım güncellenir.
//
//  Koordinat sistemi: yerel Kartezyen / ENU, metre.
//    x = Doğu, y = Kuzey, z = Yukarı; orijin = GCS başlangıç konumu (V9)
// ============================================================================

#pragma once

#include <cstdint>

namespace swarm {

struct DroneState
{
    // Konum (metre)
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;

    // Hız (metre/saniye)
    double vx = 0.0;
    double vy = 0.0;
    double vz = 0.0;

    // Batarya yüzdesi (0-100)
    uint8_t battery = 100;

    // Tüm hız bileşenlerini sıfırlar — "olduğun yerde dur".
    void reset_velocity()
    {
        vx = 0.0;
        vy = 0.0;
        vz = 0.0;
    }
};

}  // namespace swarm
