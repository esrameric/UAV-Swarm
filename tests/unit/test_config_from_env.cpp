// ============================================================================
//  Faz 4.3 — Ortam değişkeni okumanın testi
//
//  main() bir birim testinden çağrılamaz; bu yüzden yapılandırma mantığı
//  ayrı bir birime (config_from_env) çıkarıldı ve hata durumunda `exit()`
//  yerine sonuç nesnesi döndürüyor. Böylece geçersiz girdiler süreç
//  ölmeden sınanabiliyor.
// ============================================================================

#include <gtest/gtest.h>

#include "swarm/config_from_env.hpp"

#include <cstdlib>

namespace {

// Test öncesi ortamı temiz bir başlangıca çeker.
void clear_environment()
{
    // unsetenv: ortam değişkenini tamamen kaldırır (boş bırakmaz).
    ::unsetenv("NODE_TYPE");
    ::unsetenv("DRONE_ID");
    ::unsetenv("ROLE");
    ::unsetenv("ROS_DOMAIN_ID");
    ::unsetenv("INITIAL_BATTERY");
}

void set_env(const char* name, const char* value)
{
    // setenv'in üçüncü parametresi "var olanı ez" anlamına gelir.
    ::setenv(name, value, 1);
}

}  // namespace

TEST(ConfigFromEnv, HicDegiskenYokkenVarsayilanlarKullanilir)
{
    clear_environment();

    const swarm::ConfigResult result = swarm::read_config_from_env();

    ASSERT_TRUE(result.success) << result.error;
    EXPECT_EQ(result.config.node_type, swarm::NodeType::DRONE);
    EXPECT_EQ(result.config.drone_id, 0u);
    EXPECT_EQ(result.config.role, swarm::DroneRole::SCOUT);
    EXPECT_EQ(result.config.domain_id, 42u);
    EXPECT_EQ(result.starting_battery, 100u);
}

TEST(ConfigFromEnv, GcsKimligiOkunur)
{
    clear_environment();
    set_env("NODE_TYPE", "GCS");
    set_env("DRONE_ID", "0");
    set_env("ROS_DOMAIN_ID", "42");

    const swarm::ConfigResult result = swarm::read_config_from_env();

    ASSERT_TRUE(result.success) << result.error;
    EXPECT_EQ(result.config.node_type, swarm::NodeType::GCS);
    EXPECT_EQ(result.config.drone_id, 0u);
}

TEST(ConfigFromEnv, ScoutDroneKimligiOkunur)
{
    // docker-compose.yml'deki drone_scout servisinin ayarları (Bölüm 4).
    clear_environment();
    set_env("NODE_TYPE", "DRONE");
    set_env("ROLE", "SCOUT");
    set_env("DRONE_ID", "1");
    set_env("ROS_DOMAIN_ID", "42");

    const swarm::ConfigResult result = swarm::read_config_from_env();

    ASSERT_TRUE(result.success) << result.error;
    EXPECT_EQ(result.config.node_type, swarm::NodeType::DRONE);
    EXPECT_EQ(result.config.role, swarm::DroneRole::SCOUT);
    EXPECT_EQ(result.config.drone_id, 1u);
    EXPECT_EQ(result.config.domain_id, 42u);
}

TEST(ConfigFromEnv, StrikerDroneKimligiOkunur)
{
    clear_environment();
    set_env("NODE_TYPE", "DRONE");
    set_env("ROLE", "STRIKER");
    set_env("DRONE_ID", "3");

    const swarm::ConfigResult result = swarm::read_config_from_env();

    ASSERT_TRUE(result.success) << result.error;
    EXPECT_EQ(result.config.role, swarm::DroneRole::STRIKER);
    EXPECT_EQ(result.config.drone_id, 3u);
}

TEST(ConfigFromEnv, GcsIcinRoleOkunmaz)
{
    // ROLE yalnızca node_type == DRONE iken anlamlıdır (Bölüm 3.5).
    // GCS'te geçersiz bir ROLE bile hata vermemeli, çünkü okunmuyor.
    clear_environment();
    set_env("NODE_TYPE", "GCS");
    set_env("ROLE", "SACMA_BIR_DEGER");

    const swarm::ConfigResult result = swarm::read_config_from_env();

    EXPECT_TRUE(result.success) << result.error;
}

TEST(ConfigFromEnv, GecersizNodeTypeHataVerir)
{
    // Sessizce varsayılana düşmek yanlış yapılandırmayı saatlerce
    // gizleyebilir; erken ve gürültülü hata veriyoruz.
    clear_environment();
    set_env("NODE_TYPE", "SUNUCU");

    const swarm::ConfigResult result = swarm::read_config_from_env();

    EXPECT_FALSE(result.success);
    EXPECT_NE(result.error.find("NODE_TYPE"), std::string::npos);
}

TEST(ConfigFromEnv, GecersizRoleHataVerir)
{
    clear_environment();
    set_env("NODE_TYPE", "DRONE");
    set_env("ROLE", "KAMIKAZE");

    const swarm::ConfigResult result = swarm::read_config_from_env();

    EXPECT_FALSE(result.success);
    EXPECT_NE(result.error.find("ROLE"), std::string::npos);
}

TEST(ConfigFromEnv, SayiOlmayanDroneIdHataVerir)
{
    clear_environment();
    set_env("DRONE_ID", "bir");

    const swarm::ConfigResult result = swarm::read_config_from_env();

    EXPECT_FALSE(result.success);
    EXPECT_NE(result.error.find("DRONE_ID"), std::string::npos);
}

TEST(ConfigFromEnv, AraligiAsanDroneIdHataVerir)
{
    // drone_id uint8_t'dir; 300 sessizce 44'e dönüşmemeli.
    clear_environment();
    set_env("DRONE_ID", "300");

    const swarm::ConfigResult result = swarm::read_config_from_env();

    EXPECT_FALSE(result.success);
    EXPECT_NE(result.error.find("DRONE_ID"), std::string::npos);
}

TEST(ConfigFromEnv, AraligiAsanBataryaHataVerir)
{
    clear_environment();
    set_env("INITIAL_BATTERY", "150");

    const swarm::ConfigResult result = swarm::read_config_from_env();

    EXPECT_FALSE(result.success);
    EXPECT_NE(result.error.find("INITIAL_BATTERY"), std::string::npos);
}

TEST(ConfigFromEnv, DusukBataryaAyarlanabilir)
{
    // Faz 6.3'teki NACK senaryosu bu değişkenle kuruluyor: bataryası
    // kritik olan drone consensus'ta NACK verir.
    clear_environment();
    set_env("INITIAL_BATTERY", "5");

    const swarm::ConfigResult result = swarm::read_config_from_env();

    ASSERT_TRUE(result.success) << result.error;
    EXPECT_EQ(result.starting_battery, 5u);
}

TEST(ConfigFromEnv, BosDegiskenVarsayilanSayilir)
{
    // docker-compose bazen değişkeni boş string olarak geçirebilir.
    clear_environment();
    set_env("ROS_DOMAIN_ID", "");

    const swarm::ConfigResult result = swarm::read_config_from_env();

    ASSERT_TRUE(result.success) << result.error;
    EXPECT_EQ(result.config.domain_id, 42u);
}

TEST(ConfigFromEnv, AriziEnjeksiyonuVarsayilanKapali)
{
    clear_environment();

    const swarm::ConfigResult result = swarm::read_config_from_env();

    ASSERT_TRUE(result.success) << result.error;
    EXPECT_FALSE(result.config.fault_silent_consensus);
}

TEST(ConfigFromEnv, AriziEnjeksiyonuAcilabilir)
{
    // Faz 6.3: "ayakta ama sessiz" drone senaryosu bu değişkenle kuruluyor.
    clear_environment();
    set_env("FAULT_SILENT_CONSENSUS", "1");

    const swarm::ConfigResult result = swarm::read_config_from_env();

    ASSERT_TRUE(result.success) << result.error;
    EXPECT_TRUE(result.config.fault_silent_consensus);
}

TEST(ConfigFromEnv, GecersizAriziEnjeksiyonuDegeriHataVerir)
{
    clear_environment();
    set_env("FAULT_SILENT_CONSENSUS", "7");

    const swarm::ConfigResult result = swarm::read_config_from_env();

    EXPECT_FALSE(result.success);
    EXPECT_NE(result.error.find("FAULT_SILENT_CONSENSUS"), std::string::npos);
}
