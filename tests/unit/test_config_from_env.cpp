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
void ortami_temizle()
{
    // unsetenv: ortam değişkenini tamamen kaldırır (boş bırakmaz).
    ::unsetenv("NODE_TYPE");
    ::unsetenv("DRONE_ID");
    ::unsetenv("ROLE");
    ::unsetenv("ROS_DOMAIN_ID");
    ::unsetenv("INITIAL_BATTERY");
}

void ayarla(const char* isim, const char* deger)
{
    // setenv'in üçüncü parametresi "var olanı ez" anlamına gelir.
    ::setenv(isim, deger, 1);
}

}  // namespace

TEST(ConfigFromEnv, HicDegiskenYokkenVarsayilanlarKullanilir)
{
    ortami_temizle();

    const swarm::ConfigSonucu sonuc = swarm::config_ortamdan_oku();

    ASSERT_TRUE(sonuc.basarili) << sonuc.hata;
    EXPECT_EQ(sonuc.config.node_type, swarm::NodeType::DRONE);
    EXPECT_EQ(sonuc.config.drone_id, 0u);
    EXPECT_EQ(sonuc.config.role, swarm::DroneRole::SCOUT);
    EXPECT_EQ(sonuc.config.domain_id, 42u);
    EXPECT_EQ(sonuc.baslangic_bataryasi, 100u);
}

TEST(ConfigFromEnv, GcsKimligiOkunur)
{
    ortami_temizle();
    ayarla("NODE_TYPE", "GCS");
    ayarla("DRONE_ID", "0");
    ayarla("ROS_DOMAIN_ID", "42");

    const swarm::ConfigSonucu sonuc = swarm::config_ortamdan_oku();

    ASSERT_TRUE(sonuc.basarili) << sonuc.hata;
    EXPECT_EQ(sonuc.config.node_type, swarm::NodeType::GCS);
    EXPECT_EQ(sonuc.config.drone_id, 0u);
}

TEST(ConfigFromEnv, ScoutDroneKimligiOkunur)
{
    // docker-compose.yml'deki drone_scout servisinin ayarları (Bölüm 4).
    ortami_temizle();
    ayarla("NODE_TYPE", "DRONE");
    ayarla("ROLE", "SCOUT");
    ayarla("DRONE_ID", "1");
    ayarla("ROS_DOMAIN_ID", "42");

    const swarm::ConfigSonucu sonuc = swarm::config_ortamdan_oku();

    ASSERT_TRUE(sonuc.basarili) << sonuc.hata;
    EXPECT_EQ(sonuc.config.node_type, swarm::NodeType::DRONE);
    EXPECT_EQ(sonuc.config.role, swarm::DroneRole::SCOUT);
    EXPECT_EQ(sonuc.config.drone_id, 1u);
    EXPECT_EQ(sonuc.config.domain_id, 42u);
}

TEST(ConfigFromEnv, StrikerDroneKimligiOkunur)
{
    ortami_temizle();
    ayarla("NODE_TYPE", "DRONE");
    ayarla("ROLE", "STRIKER");
    ayarla("DRONE_ID", "3");

    const swarm::ConfigSonucu sonuc = swarm::config_ortamdan_oku();

    ASSERT_TRUE(sonuc.basarili) << sonuc.hata;
    EXPECT_EQ(sonuc.config.role, swarm::DroneRole::STRIKER);
    EXPECT_EQ(sonuc.config.drone_id, 3u);
}

TEST(ConfigFromEnv, GcsIcinRoleOkunmaz)
{
    // ROLE yalnızca node_type == DRONE iken anlamlıdır (Bölüm 3.5).
    // GCS'te geçersiz bir ROLE bile hata vermemeli, çünkü okunmuyor.
    ortami_temizle();
    ayarla("NODE_TYPE", "GCS");
    ayarla("ROLE", "SACMA_BIR_DEGER");

    const swarm::ConfigSonucu sonuc = swarm::config_ortamdan_oku();

    EXPECT_TRUE(sonuc.basarili) << sonuc.hata;
}

TEST(ConfigFromEnv, GecersizNodeTypeHataVerir)
{
    // Sessizce varsayılana düşmek yanlış yapılandırmayı saatlerce
    // gizleyebilir; erken ve gürültülü hata veriyoruz.
    ortami_temizle();
    ayarla("NODE_TYPE", "SUNUCU");

    const swarm::ConfigSonucu sonuc = swarm::config_ortamdan_oku();

    EXPECT_FALSE(sonuc.basarili);
    EXPECT_NE(sonuc.hata.find("NODE_TYPE"), std::string::npos);
}

TEST(ConfigFromEnv, GecersizRoleHataVerir)
{
    ortami_temizle();
    ayarla("NODE_TYPE", "DRONE");
    ayarla("ROLE", "KAMIKAZE");

    const swarm::ConfigSonucu sonuc = swarm::config_ortamdan_oku();

    EXPECT_FALSE(sonuc.basarili);
    EXPECT_NE(sonuc.hata.find("ROLE"), std::string::npos);
}

TEST(ConfigFromEnv, SayiOlmayanDroneIdHataVerir)
{
    ortami_temizle();
    ayarla("DRONE_ID", "bir");

    const swarm::ConfigSonucu sonuc = swarm::config_ortamdan_oku();

    EXPECT_FALSE(sonuc.basarili);
    EXPECT_NE(sonuc.hata.find("DRONE_ID"), std::string::npos);
}

TEST(ConfigFromEnv, AraligiAsanDroneIdHataVerir)
{
    // drone_id uint8_t'dir; 300 sessizce 44'e dönüşmemeli.
    ortami_temizle();
    ayarla("DRONE_ID", "300");

    const swarm::ConfigSonucu sonuc = swarm::config_ortamdan_oku();

    EXPECT_FALSE(sonuc.basarili);
    EXPECT_NE(sonuc.hata.find("DRONE_ID"), std::string::npos);
}

TEST(ConfigFromEnv, AraligiAsanBataryaHataVerir)
{
    ortami_temizle();
    ayarla("INITIAL_BATTERY", "150");

    const swarm::ConfigSonucu sonuc = swarm::config_ortamdan_oku();

    EXPECT_FALSE(sonuc.basarili);
    EXPECT_NE(sonuc.hata.find("INITIAL_BATTERY"), std::string::npos);
}

TEST(ConfigFromEnv, DusukBataryaAyarlanabilir)
{
    // Faz 6.3'teki NACK senaryosu bu değişkenle kuruluyor: bataryası
    // kritik olan drone consensus'ta NACK verir.
    ortami_temizle();
    ayarla("INITIAL_BATTERY", "5");

    const swarm::ConfigSonucu sonuc = swarm::config_ortamdan_oku();

    ASSERT_TRUE(sonuc.basarili) << sonuc.hata;
    EXPECT_EQ(sonuc.baslangic_bataryasi, 5u);
}

TEST(ConfigFromEnv, BosDegiskenVarsayilanSayilir)
{
    // docker-compose bazen değişkeni boş string olarak geçirebilir.
    ortami_temizle();
    ayarla("ROS_DOMAIN_ID", "");

    const swarm::ConfigSonucu sonuc = swarm::config_ortamdan_oku();

    ASSERT_TRUE(sonuc.basarili) << sonuc.hata;
    EXPECT_EQ(sonuc.config.domain_id, 42u);
}

TEST(ConfigFromEnv, AriziEnjeksiyonuVarsayilanKapali)
{
    ortami_temizle();

    const swarm::ConfigSonucu sonuc = swarm::config_ortamdan_oku();

    ASSERT_TRUE(sonuc.basarili) << sonuc.hata;
    EXPECT_FALSE(sonuc.config.fault_silent_consensus);
}

TEST(ConfigFromEnv, AriziEnjeksiyonuAcilabilir)
{
    // Faz 6.3: "ayakta ama sessiz" drone senaryosu bu değişkenle kuruluyor.
    ortami_temizle();
    ayarla("FAULT_SILENT_CONSENSUS", "1");

    const swarm::ConfigSonucu sonuc = swarm::config_ortamdan_oku();

    ASSERT_TRUE(sonuc.basarili) << sonuc.hata;
    EXPECT_TRUE(sonuc.config.fault_silent_consensus);
}

TEST(ConfigFromEnv, GecersizAriziEnjeksiyonuDegeriHataVerir)
{
    ortami_temizle();
    ayarla("FAULT_SILENT_CONSENSUS", "7");

    const swarm::ConfigSonucu sonuc = swarm::config_ortamdan_oku();

    EXPECT_FALSE(sonuc.basarili);
    EXPECT_NE(sonuc.hata.find("FAULT_SILENT_CONSENSUS"), std::string::npos);
}
