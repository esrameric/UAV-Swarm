#include "swarm/config_from_env.hpp"

#include <cstdlib>
#include <stdexcept>

namespace swarm {

namespace {

std::string env_oku(const char* isim, const std::string& varsayilan)
{
    // std::getenv, değişken tanımlı değilse nullptr döner.
    const char* deger = std::getenv(isim);
    if (deger == nullptr || std::string(deger).empty())
    {
        return varsayilan;
    }
    return std::string(deger);
}

}  // namespace

ConfigSonucu config_ortamdan_oku()
{
    ConfigSonucu sonuc;

    // --- NODE_TYPE -----------------------------------------------------------
    const std::string node_type_metni = env_oku("NODE_TYPE", "DRONE");
    if (node_type_metni == "DRONE")
    {
        sonuc.config.node_type = NodeType::DRONE;
    }
    else if (node_type_metni == "GCS")
    {
        sonuc.config.node_type = NodeType::GCS;
    }
    else
    {
        // Sessizce varsayılana düşmüyoruz: yanlış yapılandırma saatlerce
        // gizli kalabilir. Erken ve gürültülü hata vermek daha iyidir.
        sonuc.hata = "NODE_TYPE 'DRONE' veya 'GCS' olmali, gelen: '" + node_type_metni + "'";
        return sonuc;
    }

    // --- ROLE (yalnızca DRONE için anlamlı, Bölüm 3.5) ------------------------
    if (sonuc.config.node_type == NodeType::DRONE)
    {
        const std::string role_metni = env_oku("ROLE", "SCOUT");
        if (role_metni == "SCOUT")
        {
            sonuc.config.role = DroneRole::SCOUT;
        }
        else if (role_metni == "STRIKER")
        {
            sonuc.config.role = DroneRole::STRIKER;
        }
        else
        {
            sonuc.hata = "ROLE 'SCOUT' veya 'STRIKER' olmali, gelen: '" + role_metni + "'";
            return sonuc;
        }
    }

    // --- Sayısal alanlar ------------------------------------------------------
    struct SayisalAlan
    {
        const char* isim;
        unsigned long varsayilan;
        unsigned long azami;
        unsigned long deger;
    };

    SayisalAlan alanlar[] = {
        {"DRONE_ID", 0, 255, 0},
        {"ROS_DOMAIN_ID", 42, 232, 0},      // DDS domain üst sınırı 232
        {"INITIAL_BATTERY", 100, 100, 0},
    };

    for (SayisalAlan& alan : alanlar)
    {
        const std::string metin = env_oku(alan.isim, "");
        if (metin.empty())
        {
            alan.deger = alan.varsayilan;
            continue;
        }

        try
        {
            // std::stoul metni sayıya çevirir; çeviremezse istisna atar.
            alan.deger = std::stoul(metin);
        }
        catch (const std::exception&)
        {
            sonuc.hata = std::string(alan.isim) + " sayiya cevrilemedi: '" + metin + "'";
            return sonuc;
        }

        if (alan.deger > alan.azami)
        {
            sonuc.hata = std::string(alan.isim) + " en fazla " +
                         std::to_string(alan.azami) + " olabilir, gelen: " + metin;
            return sonuc;
        }
    }

    sonuc.config.drone_id = static_cast<uint8_t>(alanlar[0].deger);
    sonuc.config.domain_id = static_cast<uint32_t>(alanlar[1].deger);
    sonuc.baslangic_bataryasi = static_cast<uint8_t>(alanlar[2].deger);

    sonuc.basarili = true;
    return sonuc;
}

}  // namespace swarm
