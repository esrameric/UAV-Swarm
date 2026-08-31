// ============================================================================
//  Faz 1.5 — PeerManager testi
//
//  Odak: OFFLINE -> ONLINE geçişinde last_seen_seq'in sıfırlanması.
//  Bu, bir drone yeniden başladığında taze verisinin "bayat" diye
//  reddedilmesini engelleyen kritik davranıştır (Bölüm 3.5).
//
//  Zaman testlerde gerçekten beklenmez: PeerManager fonksiyonları `now`
//  parametresi aldığı için, 3 saniyelik zaman aşımını "3 saniye ileri bir
//  zaman değeri vererek" anında test edebiliyoruz.
// ============================================================================

#include <gtest/gtest.h>

#include "swarm/peer_manager.hpp"

#include <chrono>

namespace {

using namespace std::chrono_literals;

// Testlerde tekrar tekrar heartbeat kurmamak için küçük bir yardımcı.
swarm::Heartbeat create_heartbeat(
        uint8_t drone_id,
        swarm::TaskType task = swarm::TaskType::IDLE)
{
    swarm::Heartbeat heartbeat;
    heartbeat.drone_id(drone_id);
    heartbeat.node_type(swarm::NodeType::DRONE);
    heartbeat.role(swarm::DroneRole::SCOUT);
    heartbeat.current_task(task);
    return heartbeat;
}

swarm::Telemetry create_telemetry(uint8_t drone_id, uint32_t seq_num)
{
    swarm::Telemetry telemetry;
    telemetry.drone_id(drone_id);
    telemetry.seq_num(seq_num);
    return telemetry;
}

// Testlerin başlangıç zamanı. Gerçek saatten bağımsız, sabit bir referans.
const std::chrono::steady_clock::time_point START{};

}  // namespace

// ---------------------------------------------------------------------------
//  Temel davranış
// ---------------------------------------------------------------------------

TEST(PeerManager, BaslangictaTabloBos)
{
    const swarm::PeerManager manager;

    EXPECT_EQ(manager.peer_count(), 0u);
    EXPECT_EQ(manager.status_of(1), swarm::PeerStatus::OFFLINE);
    EXPECT_EQ(manager.find(1), nullptr);
}

TEST(PeerManager, IlkHeartbeatPeeriEklerVeOnlineYapar)
{
    swarm::PeerManager manager;

    manager.on_heartbeat(create_heartbeat(1, swarm::TaskType::DISCOVERY), START);

    EXPECT_EQ(manager.peer_count(), 1u);
    EXPECT_EQ(manager.status_of(1), swarm::PeerStatus::ONLINE);

    const swarm::PeerRecord* record = manager.find(1);
    ASSERT_NE(record, nullptr);
    EXPECT_EQ(record->info.drone_id, 1u);
    EXPECT_EQ(record->info.current_task, swarm::TaskType::DISCOVERY);
    EXPECT_EQ(record->info.last_heartbeat_local, START);
}

TEST(PeerManager, HeartbeatAktifGoreviTazeler)
{
    swarm::PeerManager manager;

    manager.on_heartbeat(create_heartbeat(2, swarm::TaskType::IDLE), START);
    manager.on_heartbeat(create_heartbeat(2, swarm::TaskType::GO_TO_TARGET),
                          START + 100ms);

    const swarm::PeerRecord* record = manager.find(2);
    ASSERT_NE(record, nullptr);
    EXPECT_EQ(record->info.current_task, swarm::TaskType::GO_TO_TARGET);
    EXPECT_EQ(manager.peer_count(), 1u);  // aynı peer, ikinci kayıt açılmadı
}

// ---------------------------------------------------------------------------
//  Zaman aşımı
// ---------------------------------------------------------------------------

TEST(PeerManager, TimeoutDolmadanOnlineKalir)
{
    swarm::PeerManager manager{3000ms};
    manager.on_heartbeat(create_heartbeat(1), START);

    manager.refresh_status(START + 2999ms);

    EXPECT_EQ(manager.status_of(1), swarm::PeerStatus::ONLINE);
}

TEST(PeerManager, TimeoutDolunsaOfflineOlur)
{
    swarm::PeerManager manager{3000ms};
    manager.on_heartbeat(create_heartbeat(1), START);

    manager.refresh_status(START + 3000ms);

    EXPECT_EQ(manager.status_of(1), swarm::PeerStatus::OFFLINE);
}

// ---------------------------------------------------------------------------
//  Telemetri bayatlık kontrolü
// ---------------------------------------------------------------------------

TEST(PeerManager, TanimadigiPeerinTelemetrisiReddedilir)
{
    swarm::PeerManager manager;

    // Heartbeat ile tanışmadığımız bir drone'un telemetrisi tabloya
    // kayıt AÇMAMALI.
    EXPECT_FALSE(manager.on_telemetry(create_telemetry(9, 1), START));
    EXPECT_EQ(manager.peer_count(), 0u);
}

TEST(PeerManager, ArtanSeqNumKabulEdilir)
{
    swarm::PeerManager manager;
    manager.on_heartbeat(create_heartbeat(1), START);

    EXPECT_TRUE(manager.on_telemetry(create_telemetry(1, 1), START));
    EXPECT_TRUE(manager.on_telemetry(create_telemetry(1, 2), START));
    EXPECT_TRUE(manager.on_telemetry(create_telemetry(1, 10), START));

    EXPECT_EQ(manager.find(1)->info.last_seen_seq, 10u);
}

TEST(PeerManager, GeriKalanSeqNumReddedilir)
{
    // UDP paket sırasını bozabilir: 10'dan sonra gelen 7 BAYATtır, atılmalı.
    swarm::PeerManager manager;
    manager.on_heartbeat(create_heartbeat(1), START);
    ASSERT_TRUE(manager.on_telemetry(create_telemetry(1, 10), START));

    EXPECT_FALSE(manager.on_telemetry(create_telemetry(1, 7), START));

    // Reddedilen paket sayacı geriye çekmemeli.
    EXPECT_EQ(manager.find(1)->info.last_seen_seq, 10u);
}

TEST(PeerManager, AyniSeqNumTekrariReddedilir)
{
    swarm::PeerManager manager;
    manager.on_heartbeat(create_heartbeat(1), START);
    ASSERT_TRUE(manager.on_telemetry(create_telemetry(1, 5), START));

    EXPECT_FALSE(manager.on_telemetry(create_telemetry(1, 5), START));
}

// ---------------------------------------------------------------------------
//  ASIL SENARYO: OFFLINE -> ONLINE geçişinde last_seen_seq sıfırlanır
// ---------------------------------------------------------------------------

TEST(PeerManager, RestartSonrasiSeqSifirlanirVeTazeVeriKabulEdilir)
{
    swarm::PeerManager manager{3000ms};

    // 1) Drone ayakta, telemetri sayacı yükseliyor.
    manager.on_heartbeat(create_heartbeat(1), START);
    ASSERT_TRUE(manager.on_telemetry(create_telemetry(1, 5000), START));
    ASSERT_EQ(manager.find(1)->info.last_seen_seq, 5000u);

    // 2) Drone susuyor; zaman aşımı dolunca OFFLINE'a düşüyor.
    manager.refresh_status(START + 5s);
    ASSERT_EQ(manager.status_of(1), swarm::PeerStatus::OFFLINE);

    // 3) Drone yeniden başlıyor ve heartbeat yolluyor.
    //    OFFLINE -> ONLINE geçişi burada olur ve sayaç sıfırlanır.
    manager.on_heartbeat(create_heartbeat(1), START + 6s);

    EXPECT_EQ(manager.status_of(1), swarm::PeerStatus::ONLINE);
    EXPECT_EQ(manager.find(1)->info.last_seen_seq, 0u);

    // 4) Restart sonrası taze telemetri 1'den başlıyor. Sayaç sıfırlanmamış
    //    olsaydı bu paket "5000'den küçük" diye reddedilir ve drone bir daha
    //    hiç görünmezdi. İşte bu testin koruduğu hata budur.
    EXPECT_TRUE(manager.on_telemetry(create_telemetry(1, 1), START + 6s));
    EXPECT_EQ(manager.find(1)->info.last_seen_seq, 1u);
}

TEST(PeerManager, OnlineKalanPeerinSeqSayaciSifirlanmaz)
{
    // Ters kontrol: peer hiç OFFLINE'a düşmediyse, gelen her heartbeat
    // sayacı sıfırlamamalı. Aksi halde her heartbeat bayatlık korumasını
    // silerdi.
    swarm::PeerManager manager{3000ms};

    manager.on_heartbeat(create_heartbeat(1), START);
    ASSERT_TRUE(manager.on_telemetry(create_telemetry(1, 100), START));

    manager.on_heartbeat(create_heartbeat(1), START + 1s);

    EXPECT_EQ(manager.find(1)->info.last_seen_seq, 100u);
}

TEST(PeerManager, BirPeerinOfflineOlmasiDigeriniEtkilemez)
{
    swarm::PeerManager manager{3000ms};

    manager.on_heartbeat(create_heartbeat(1), START);
    manager.on_heartbeat(create_heartbeat(2), START);
    ASSERT_TRUE(manager.on_telemetry(create_telemetry(2, 50), START));

    // Sadece 2 numara heartbeat yollamaya devam ediyor.
    manager.on_heartbeat(create_heartbeat(2), START + 2s);
    manager.refresh_status(START + 4s);

    EXPECT_EQ(manager.status_of(1), swarm::PeerStatus::OFFLINE);
    EXPECT_EQ(manager.status_of(2), swarm::PeerStatus::ONLINE);
    EXPECT_EQ(manager.find(2)->info.last_seen_seq, 50u);
}

// ---------------------------------------------------------------------------
//  Hızlı restart: OFFLINE'a hiç düşmeden yeniden başlama
//
//  Faz 6.5 entegrasyon testinin ortaya çıkardığı açık. OFFLINE -> ONLINE
//  sıfırlaması tek başına yetmiyor: bir drone heartbeat zaman aşımından
//  (3 sn) daha hızlı yeniden başlarsa hiç OFFLINE görünmez, sayaç
//  sıfırlanmaz ve taze verisi sessizce reddedilir.
// ---------------------------------------------------------------------------

TEST(PeerManager, HizliRestartBuyukGeriSicramaylaTespitEdilir)
{
    swarm::PeerManager manager{3000ms};
    manager.on_heartbeat(create_heartbeat(1), START);

    // Sayaç epeyce ilerlemiş.
    ASSERT_TRUE(manager.on_telemetry(create_telemetry(1, 5000), START));

    // Drone 1 saniyede yeniden başlıyor: OFFLINE'a hiç düşmüyor.
    manager.on_heartbeat(create_heartbeat(1), START + 1s);
    ASSERT_EQ(manager.status_of(1), swarm::PeerStatus::ONLINE);
    ASSERT_EQ(manager.find(1)->info.last_seen_seq, 5000u)
            << "OFFLINE'a dusmedigi icin sayac sifirlanmamis olmali";

    // Restart sonrası taze telemetri 1'den başlıyor. Büyük geri sıçrama
    // tespit edilip takip sıfırlanmalı ve paket KABUL edilmeli.
    bool new_stream = false;
    EXPECT_TRUE(manager.on_telemetry(create_telemetry(1, 1), START + 1s, &new_stream));
    EXPECT_TRUE(new_stream);
    EXPECT_EQ(manager.find(1)->info.last_seen_seq, 1u);
}

TEST(PeerManager, KucukGeriSicramaHalaBayatSayilir)
{
    // Restart koruması, asıl bayatlık korumasını ZAYIFLATMAMALI. UDP'de
    // sıra bozulması birkaç paketliktir; böyle bir paket hâlâ atılmalı.
    swarm::PeerManager manager{3000ms};
    manager.on_heartbeat(create_heartbeat(1), START);
    ASSERT_TRUE(manager.on_telemetry(create_telemetry(1, 5000), START));

    EXPECT_FALSE(manager.on_telemetry(create_telemetry(1, 4990), START));
    EXPECT_EQ(manager.find(1)->info.last_seen_seq, 5000u);
}

TEST(PeerManager, RestartEsigiSinirindaDavranis)
{
    swarm::PeerManager manager{3000ms};
    manager.on_heartbeat(create_heartbeat(1), START);
    ASSERT_TRUE(manager.on_telemetry(create_telemetry(1, 1000), START));

    // Eşik kadar geri: henüz restart sayılmaz, bayat kabul edilir.
    const uint32_t threshold = swarm::PeerManager::RESTART_DETECTION_THRESHOLD;
    EXPECT_FALSE(manager.on_telemetry(create_telemetry(1, 1000 - threshold), START));

    // Eşiğin bir fazlası kadar geri: restart sayılır, kabul edilir.
    EXPECT_TRUE(manager.on_telemetry(create_telemetry(1, 1000 - threshold - 1), START));
}

TEST(PeerManager, IlkTelemetriYeniAkisOlarakIsaretlenir)
{
    swarm::PeerManager manager;
    manager.on_heartbeat(create_heartbeat(1), START);

    bool new_stream = false;
    EXPECT_TRUE(manager.on_telemetry(create_telemetry(1, 1), START, &new_stream));
    EXPECT_TRUE(new_stream);

    // İkinci paket yeni akış değil.
    EXPECT_TRUE(manager.on_telemetry(create_telemetry(1, 2), START, &new_stream));
    EXPECT_FALSE(new_stream);
}
