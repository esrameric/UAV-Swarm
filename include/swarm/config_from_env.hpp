// ============================================================================
//  Ortam değişkenlerinden SwarmConfig üretimi (Bölüm 4)
//
//      NODE_TYPE        DRONE | GCS          (varsayılan: DRONE)
//      DRONE_ID         0-255                (varsayılan: 0)
//      ROLE             SCOUT | STRIKER      (varsayılan: SCOUT)
//      ROS_DOMAIN_ID    DDS domain numarası  (varsayılan: 42)
//      INITIAL_BATTERY  0-100                (varsayılan: 100)
//      FAULT_SILENT_CONSENSUS  0 | 1         (varsayılan: 0)
//      NODE_IP          bu düğümün kendi IP'si   (varsayılan: yok -> TCP kapalı)
//      TCP_PORT         TCP dinleme portu        (varsayılan: 5100)
//
//  NODE_IP verilmezse TCP taşıyıcısı kurulmaz ve tüm topic'ler UDP'den gider.
//  Bilinçli bir tercih: düğüm kendi adresini bilmeden karşı tarafa "bana
//  buradan ulaş" diyemez, uydurma bir adres ilan etmek ise sessiz bir
//  iletişim kopukluğu yaratırdı.
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

#include "swarm/fastdds_wrapper.hpp"
#include "swarm/swarm_manager.hpp"

namespace swarm {

struct ConfigResult
{
    bool success = false;
    std::string error;          // basarili == false iken doludur
    SwarmConfig config{};
    uint8_t starting_battery = 100;

    // Taşıyıcı ayarı SwarmConfig'in İÇİNDE değil: SwarmConfig uygulama
    // mantığının yapılandırmasıdır ve SwarmManager DDS'i tanımaz (bkz. V19).
    TcpTransportConfig tcp{};
};

// Ortam değişkenlerini okuyup doğrular.
ConfigResult read_config_from_env();

}  // namespace swarm
