#include "swarm/config_from_env.hpp"

#include <cstdlib>
#include <stdexcept>

namespace swarm {

namespace {

std::string read_env(const char* name, const std::string& default_value)
{
    // std::getenv, değişken tanımlı değilse nullptr döner.
    const char* value = std::getenv(name);
    if (value == nullptr || std::string(value).empty())
    {
        return default_value;
    }
    return std::string(value);
}

}  // namespace

ConfigResult read_config_from_env()
{
    ConfigResult result;

    // --- NODE_TYPE -----------------------------------------------------------
    const std::string node_type_text = read_env("NODE_TYPE", "DRONE");
    if (node_type_text == "DRONE")
    {
        result.config.node_type = NodeType::DRONE;
    }
    else if (node_type_text == "GCS")
    {
        result.config.node_type = NodeType::GCS;
    }
    else
    {
        // Sessizce varsayılana düşmüyoruz: yanlış yapılandırma saatlerce
        // gizli kalabilir. Erken ve gürültülü hata vermek daha iyidir.
        result.error = "NODE_TYPE 'DRONE' veya 'GCS' olmali, gelen: '" + node_type_text + "'";
        return result;
    }

    // --- ROLE (yalnızca DRONE için anlamlı, Bölüm 3.5) ------------------------
    if (result.config.node_type == NodeType::DRONE)
    {
        const std::string role_text = read_env("ROLE", "SCOUT");
        if (role_text == "SCOUT")
        {
            result.config.role = DroneRole::SCOUT;
        }
        else if (role_text == "STRIKER")
        {
            result.config.role = DroneRole::STRIKER;
        }
        else
        {
            result.error = "ROLE 'SCOUT' veya 'STRIKER' olmali, gelen: '" + role_text + "'";
            return result;
        }
    }

    // --- Sayısal alanlar ------------------------------------------------------
    struct NumericField
    {
        const char* name;
        unsigned long default_value;
        unsigned long max;
        unsigned long value;
    };

    NumericField fields[] = {
        {"DRONE_ID", 0, 255, 0},
        {"ROS_DOMAIN_ID", 42, 232, 0},      // DDS domain üst sınırı 232
        {"INITIAL_BATTERY", 100, 100, 0},
        {"FAULT_SILENT_CONSENSUS", 0, 1, 0},
        {"TCP_PORT", 5100, 65535, 0},
    };

    for (NumericField& field : fields)
    {
        const std::string text = read_env(field.name, "");
        if (text.empty())
        {
            field.value = field.default_value;
            continue;
        }

        try
        {
            // std::stoul metni sayıya çevirir; çeviremezse istisna atar.
            field.value = std::stoul(text);
        }
        catch (const std::exception&)
        {
            result.error = std::string(field.name) + " sayiya cevrilemedi: '" + text + "'";
            return result;
        }

        if (field.value > field.max)
        {
            result.error = std::string(field.name) + " en fazla " +
                         std::to_string(field.max) + " olabilir, gelen: " + text;
            return result;
        }
    }

    result.config.drone_id = static_cast<uint8_t>(fields[0].value);
    result.config.domain_id = static_cast<uint32_t>(fields[1].value);
    result.starting_battery = static_cast<uint8_t>(fields[2].value);
    result.config.fault_silent_consensus = (fields[3].value == 1);

    // --- TCP taşıyıcısı (Bölüm 3.4) -----------------------------------------
    // NODE_IP yoksa TCP kurulmaz; sistem tümüyle UDP'ye döner. Port 0 geçerli
    // bir dinleme portu değildir, o yüzden ayrıca reddediliyor.
    const std::string node_ip = read_env("NODE_IP", "");
    if (!node_ip.empty())
    {
        if (fields[4].value == 0)
        {
            result.error = "TCP_PORT 0 olamaz";
            return result;
        }

        result.tcp.enabled = true;
        result.tcp.local_ip = node_ip;
        result.tcp.listening_port = static_cast<uint16_t>(fields[4].value);
    }

    result.success = true;
    return result;
}

}  // namespace swarm
