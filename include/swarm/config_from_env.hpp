// ============================================================================
//  Ortam değişkenlerinden SwarmConfig üretimi (Bölüm 4)
//
//      NODE_TYPE        DRONE | GCS          (varsayılan: DRONE)
//      DRONE_ID         0-255                (varsayılan: 0)
//      ROLE             SCOUT | STRIKER      (varsayılan: SCOUT)
//      ROS_DOMAIN_ID    DDS domain numarası  (varsayılan: 42)
//      INITIAL_BATTERY  0-100                (varsayılan: 100)
//
//  Neden ayrı bir dosya? Bu mantık main.cpp içinde olsaydı test edilemezdi:
//  main() bir birim testinden çağrılamaz. Ayrıca hata durumunda burada
//  `exit()` ÇAĞRILMIYOR — hata bir sonuç nesnesiyle geri veriliyor, programı
//  sonlandırma kararını main() veriyor. Test, geçersiz girdiyi süreç
//  ölmeden sınayabiliyor.
// ============================================================================

#pragma once

#include <cstdint>
#include <string>

#include "swarm/swarm_manager.hpp"

namespace swarm {

struct ConfigSonucu
{
    bool basarili = false;
    std::string hata;          // basarili == false iken doludur
    SwarmConfig config{};
    uint8_t baslangic_bataryasi = 100;
};

// Ortam değişkenlerini okuyup doğrular.
ConfigSonucu config_ortamdan_oku();

}  // namespace swarm
