// ============================================================================
//  Faz 3.6 — GcsController testi
//
//  Uçtan uca senaryo: GCS teklif eder -> drone'lar oy verir -> oybirliği
//  sağlanırsa görev emri yayınlanır, sağlanmazsa yayınlanmaz.
//
//  Bu test aynı zamanda 2PC akışının (Bölüm 3.6) tamamını tek yerde
//  gösteren okunabilir bir örnek.
// ============================================================================

#include <gtest/gtest.h>

#include "swarm/gcs_controller.hpp"
#include "swarm/swarm_manager.hpp"

#include <chrono>
#include <vector>

namespace {

using namespace std::chrono_literals;

const swarm::TimePoint BASLANGIC{};

// GCS kimliğiyle hazırlanmış, temiz bir SwarmManager.
swarm::SwarmManager& gcs_olarak_hazirla()
{
    swarm::SwarmManager& yonetici = swarm::SwarmManager::get_instance();

    swarm::SwarmConfig config;
    config.drone_id = 0;
    config.node_type = swarm::NodeType::GCS;
    yonetici.init(config);
    yonetici.set_consensus_publisher(nullptr);
    yonetici.set_task_allocation_publisher(nullptr);

    return yonetici;
}

swarm::Heartbeat drone_heartbeat(uint8_t drone_id, swarm::DroneRole rol)
{
    swarm::Heartbeat kalp_atisi;
    kalp_atisi.drone_id(drone_id);
    kalp_atisi.node_type(swarm::NodeType::DRONE);
    kalp_atisi.role(rol);
    kalp_atisi.current_task(swarm::TaskType::IDLE);
    return kalp_atisi;
}

// Bir drone'un oyunu komut kuyruğuna koyar (ağdan gelmiş gibi).
void oy_gonder(swarm::SwarmManager& yonetici,
               uint32_t transaction_id,
               uint8_t drone_id,
               swarm::Vote oy)
{
    swarm::Consensus mesaj;
    mesaj.transaction_id(transaction_id);
    mesaj.sender_id(drone_id);
    mesaj.vote(oy);
    yonetici.add_command(swarm::Command::consensus_oyu(mesaj));
}

// Üç drone'u ONLINE yapar (1 Scout + 2 Striker — Bölüm 3.1'deki sürü).
void suruyu_ayaga_kaldir(swarm::SwarmManager& yonetici)
{
    yonetici.on_heartbeat_received(drone_heartbeat(1, swarm::DroneRole::SCOUT), BASLANGIC);
    yonetici.on_heartbeat_received(drone_heartbeat(2, swarm::DroneRole::STRIKER), BASLANGIC);
    yonetici.on_heartbeat_received(drone_heartbeat(3, swarm::DroneRole::STRIKER), BASLANGIC);
}

}  // namespace

TEST(GcsController, BaslangictaBosta)
{
    swarm::SwarmManager& yonetici = gcs_olarak_hazirla();
    const swarm::GcsController gcs{yonetici};

    EXPECT_EQ(gcs.durum(), swarm::GcsController::Durum::BOSTA);
}

TEST(GcsController, TeklifYayinlanirVeOylamayaGecilir)
{
    swarm::SwarmManager& yonetici = gcs_olarak_hazirla();
    suruyu_ayaga_kaldir(yonetici);

    // Yayınlanan consensus mesajlarını yakalıyoruz.
    std::vector<swarm::Consensus> yayinlananlar;
    yonetici.set_consensus_publisher(
            [&yayinlananlar](const swarm::Consensus& mesaj) {
                yayinlananlar.push_back(mesaj);
            });

    swarm::GcsController gcs{yonetici};
    const uint32_t islem_no = gcs.gorev_teklif_et(
            swarm::DroneRole::STRIKER, 100.0, 50.0, BASLANGIC);

    EXPECT_EQ(gcs.durum(), swarm::GcsController::Durum::OYLAMA);
    ASSERT_EQ(yayinlananlar.size(), 1u);

    // TEKLİF mesajının işareti: vote alanı PENDING.
    EXPECT_EQ(yayinlananlar[0].vote(), swarm::Vote::PENDING);
    EXPECT_EQ(yayinlananlar[0].transaction_id(), islem_no);
    EXPECT_EQ(yayinlananlar[0].sender_id(), 0u);

    yonetici.set_consensus_publisher(nullptr);
}

TEST(GcsController, TeklifAnindaGorevEmriYayinlanmaz)
{
    // Emir ancak oybirliğinden SONRA yayınlanır.
    swarm::SwarmManager& yonetici = gcs_olarak_hazirla();
    suruyu_ayaga_kaldir(yonetici);

    int yayinlanan_emir_sayisi = 0;
    yonetici.set_task_allocation_publisher(
            [&yayinlanan_emir_sayisi](const swarm::TaskAllocation&) {
                ++yayinlanan_emir_sayisi;
            });

    swarm::GcsController gcs{yonetici};
    gcs.gorev_teklif_et(swarm::DroneRole::SCOUT, 10.0, 10.0, BASLANGIC);

    EXPECT_EQ(yayinlanan_emir_sayisi, 0);

    yonetici.set_task_allocation_publisher(nullptr);
}

TEST(GcsController, OybirligindeGorevEmriYayinlanir)
{
    swarm::SwarmManager& yonetici = gcs_olarak_hazirla();
    suruyu_ayaga_kaldir(yonetici);

    std::vector<swarm::TaskAllocation> yayinlanan_emirler;
    yonetici.set_task_allocation_publisher(
            [&yayinlanan_emirler](const swarm::TaskAllocation& emir) {
                yayinlanan_emirler.push_back(emir);
            });

    swarm::GcsController gcs{yonetici};
    const uint32_t islem_no = gcs.gorev_teklif_et(
            swarm::DroneRole::STRIKER, 250.0, -75.0, BASLANGIC);

    // Oylama başlasın.
    yonetici.task_engine_adimi(BASLANGIC);

    // Üç drone da ACK veriyor.
    oy_gonder(yonetici, islem_no, 1, swarm::Vote::ACK);
    oy_gonder(yonetici, islem_no, 2, swarm::Vote::ACK);
    oy_gonder(yonetici, islem_no, 3, swarm::Vote::ACK);
    yonetici.task_engine_adimi(BASLANGIC + 100ms);

    gcs.adim(BASLANGIC + 100ms);

    EXPECT_EQ(gcs.durum(), swarm::GcsController::Durum::GOREV_YAYINLANDI);
    ASSERT_EQ(yayinlanan_emirler.size(), 1u);
    EXPECT_EQ(yayinlanan_emirler[0].task_id(), islem_no);
    EXPECT_EQ(yayinlanan_emirler[0].target_role(), swarm::DroneRole::STRIKER);
    EXPECT_DOUBLE_EQ(yayinlanan_emirler[0].target_x(), 250.0);
    EXPECT_DOUBLE_EQ(yayinlanan_emirler[0].target_y(), -75.0);

    yonetici.set_task_allocation_publisher(nullptr);
}

TEST(GcsController, TekNackGoreviIptalEderVeEmirYayinlanmaz)
{
    swarm::SwarmManager& yonetici = gcs_olarak_hazirla();
    suruyu_ayaga_kaldir(yonetici);

    int yayinlanan_emir_sayisi = 0;
    yonetici.set_task_allocation_publisher(
            [&yayinlanan_emir_sayisi](const swarm::TaskAllocation&) {
                ++yayinlanan_emir_sayisi;
            });

    swarm::GcsController gcs{yonetici};
    const uint32_t islem_no = gcs.gorev_teklif_et(
            swarm::DroneRole::SCOUT, 10.0, 10.0, BASLANGIC);

    yonetici.task_engine_adimi(BASLANGIC);

    oy_gonder(yonetici, islem_no, 1, swarm::Vote::ACK);
    oy_gonder(yonetici, islem_no, 2, swarm::Vote::NACK);   // biri hayır diyor
    yonetici.task_engine_adimi(BASLANGIC + 100ms);

    gcs.adim(BASLANGIC + 100ms);

    EXPECT_EQ(gcs.durum(), swarm::GcsController::Durum::IPTAL);
    EXPECT_EQ(yayinlanan_emir_sayisi, 0);

    yonetici.set_task_allocation_publisher(nullptr);
}

TEST(GcsController, CevapsizDroneVarsaBesSaniyeSonundaIptal)
{
    // Bölüm 3.6: 5 saniye içinde tam ACK sağlanamazsa görev iptal edilir.
    swarm::SwarmManager& yonetici = gcs_olarak_hazirla();
    suruyu_ayaga_kaldir(yonetici);

    int yayinlanan_emir_sayisi = 0;
    yonetici.set_task_allocation_publisher(
            [&yayinlanan_emir_sayisi](const swarm::TaskAllocation&) {
                ++yayinlanan_emir_sayisi;
            });

    swarm::GcsController gcs{yonetici};
    const uint32_t islem_no = gcs.gorev_teklif_et(
            swarm::DroneRole::SCOUT, 10.0, 10.0, BASLANGIC);

    yonetici.task_engine_adimi(BASLANGIC);

    // Yalnızca iki drone cevap veriyor; üçüncüsü sessiz.
    oy_gonder(yonetici, islem_no, 1, swarm::Vote::ACK);
    oy_gonder(yonetici, islem_no, 2, swarm::Vote::ACK);
    yonetici.task_engine_adimi(BASLANGIC + 100ms);
    gcs.adim(BASLANGIC + 100ms);
    ASSERT_EQ(gcs.durum(), swarm::GcsController::Durum::OYLAMA);

    // 5 saniye doluyor.
    yonetici.task_engine_adimi(BASLANGIC + 5s);
    gcs.adim(BASLANGIC + 5s);

    EXPECT_EQ(gcs.durum(), swarm::GcsController::Durum::IPTAL);
    EXPECT_EQ(yayinlanan_emir_sayisi, 0);
    EXPECT_TRUE(yonetici.last_consensus_result().timeout_ile_iptal);

    yonetici.set_task_allocation_publisher(nullptr);
}

TEST(GcsController, HicDroneYokkenTeklifAninaGecer)
{
    // Sürüde hiç drone yoksa oy bekleyecek kimse de yok: oylama anında
    // COMMITTED olur ve emir yayınlanır (V16).
    swarm::SwarmManager& yonetici = gcs_olarak_hazirla();

    int yayinlanan_emir_sayisi = 0;
    yonetici.set_task_allocation_publisher(
            [&yayinlanan_emir_sayisi](const swarm::TaskAllocation&) {
                ++yayinlanan_emir_sayisi;
            });

    swarm::GcsController gcs{yonetici};
    gcs.gorev_teklif_et(swarm::DroneRole::SCOUT, 5.0, 5.0, BASLANGIC);

    yonetici.task_engine_adimi(BASLANGIC);
    gcs.adim(BASLANGIC);

    EXPECT_EQ(gcs.durum(), swarm::GcsController::Durum::GOREV_YAYINLANDI);
    EXPECT_EQ(yayinlanan_emir_sayisi, 1);

    yonetici.set_task_allocation_publisher(nullptr);
}

// --- Drone tarafı: teklife oy verme ----------------------------------------

TEST(DroneOyVerme, TeklifAlanDroneAckYayinlar)
{
    swarm::SwarmManager& yonetici = swarm::SwarmManager::get_instance();
    swarm::SwarmConfig config;
    config.drone_id = 2;
    config.node_type = swarm::NodeType::DRONE;
    config.role = swarm::DroneRole::STRIKER;
    yonetici.init(config);

    std::vector<swarm::Consensus> yayinlananlar;
    yonetici.set_consensus_publisher(
            [&yayinlananlar](const swarm::Consensus& mesaj) {
                yayinlananlar.push_back(mesaj);
            });

    // GCS'ten teklif geliyor (vote = PENDING).
    swarm::Consensus teklif;
    teklif.transaction_id(55);
    teklif.sender_id(0);
    teklif.vote(swarm::Vote::PENDING);
    yonetici.add_command(swarm::Command::consensus_oyu(teklif));

    yonetici.task_engine_adimi(BASLANGIC);

    ASSERT_EQ(yayinlananlar.size(), 1u);
    EXPECT_EQ(yayinlananlar[0].transaction_id(), 55u);
    EXPECT_EQ(yayinlananlar[0].sender_id(), 2u);
    EXPECT_EQ(yayinlananlar[0].vote(), swarm::Vote::ACK);

    yonetici.set_consensus_publisher(nullptr);
}

TEST(DroneOyVerme, BataryasiKritikOlanDroneNackYayinlar)
{
    swarm::SwarmManager& yonetici = swarm::SwarmManager::get_instance();
    swarm::SwarmConfig config;
    config.drone_id = 3;
    config.node_type = swarm::NodeType::DRONE;
    config.role = swarm::DroneRole::STRIKER;
    yonetici.init(config);

    yonetici.drone_state().battery = 5;   // kritik

    std::vector<swarm::Consensus> yayinlananlar;
    yonetici.set_consensus_publisher(
            [&yayinlananlar](const swarm::Consensus& mesaj) {
                yayinlananlar.push_back(mesaj);
            });

    swarm::Consensus teklif;
    teklif.transaction_id(56);
    teklif.sender_id(0);
    teklif.vote(swarm::Vote::PENDING);
    yonetici.add_command(swarm::Command::consensus_oyu(teklif));

    yonetici.task_engine_adimi(BASLANGIC);

    ASSERT_EQ(yayinlananlar.size(), 1u);
    EXPECT_EQ(yayinlananlar[0].vote(), swarm::Vote::NACK);

    yonetici.set_consensus_publisher(nullptr);
}

TEST(DroneOyVerme, GcsKendiTeklifineOyVermez)
{
    // GCS bir drone değildir; kendi teklifine oy üretmemeli.
    swarm::SwarmManager& yonetici = gcs_olarak_hazirla();

    std::vector<swarm::Consensus> yayinlananlar;
    yonetici.set_consensus_publisher(
            [&yayinlananlar](const swarm::Consensus& mesaj) {
                yayinlananlar.push_back(mesaj);
            });

    swarm::Consensus teklif;
    teklif.transaction_id(57);
    teklif.sender_id(9);   // başka bir düğümden gelmiş gibi
    teklif.vote(swarm::Vote::PENDING);
    yonetici.add_command(swarm::Command::consensus_oyu(teklif));

    yonetici.task_engine_adimi(BASLANGIC);

    EXPECT_TRUE(yayinlananlar.empty());

    yonetici.set_consensus_publisher(nullptr);
}
