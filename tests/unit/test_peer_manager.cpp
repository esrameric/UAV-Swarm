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
swarm::Heartbeat heartbeat_olustur(
        uint8_t drone_id,
        swarm::TaskType gorev = swarm::TaskType::IDLE)
{
    swarm::Heartbeat kalp_atisi;
    kalp_atisi.drone_id(drone_id);
    kalp_atisi.node_type(swarm::NodeType::DRONE);
    kalp_atisi.role(swarm::DroneRole::SCOUT);
    kalp_atisi.current_task(gorev);
    return kalp_atisi;
}

swarm::Telemetry telemetri_olustur(uint8_t drone_id, uint32_t seq_num)
{
    swarm::Telemetry telemetri;
    telemetri.drone_id(drone_id);
    telemetri.seq_num(seq_num);
    return telemetri;
}

// Testlerin başlangıç zamanı. Gerçek saatten bağımsız, sabit bir referans.
const std::chrono::steady_clock::time_point BASLANGIC{};

}  // namespace

// ---------------------------------------------------------------------------
//  Temel davranış
// ---------------------------------------------------------------------------

TEST(PeerManager, BaslangictaTabloBos)
{
    const swarm::PeerManager yonetici;

    EXPECT_EQ(yonetici.peer_count(), 0u);
    EXPECT_EQ(yonetici.status_of(1), swarm::PeerStatus::OFFLINE);
    EXPECT_EQ(yonetici.find(1), nullptr);
}

TEST(PeerManager, IlkHeartbeatPeeriEklerVeOnlineYapar)
{
    swarm::PeerManager yonetici;

    yonetici.on_heartbeat(heartbeat_olustur(1, swarm::TaskType::DISCOVERY), BASLANGIC);

    EXPECT_EQ(yonetici.peer_count(), 1u);
    EXPECT_EQ(yonetici.status_of(1), swarm::PeerStatus::ONLINE);

    const swarm::PeerRecord* kayit = yonetici.find(1);
    ASSERT_NE(kayit, nullptr);
    EXPECT_EQ(kayit->info.drone_id, 1u);
    EXPECT_EQ(kayit->info.current_task, swarm::TaskType::DISCOVERY);
    EXPECT_EQ(kayit->info.last_heartbeat_local, BASLANGIC);
}

TEST(PeerManager, HeartbeatAktifGoreviTazeler)
{
    swarm::PeerManager yonetici;

    yonetici.on_heartbeat(heartbeat_olustur(2, swarm::TaskType::IDLE), BASLANGIC);
    yonetici.on_heartbeat(heartbeat_olustur(2, swarm::TaskType::GO_TO_TARGET),
                          BASLANGIC + 100ms);

    const swarm::PeerRecord* kayit = yonetici.find(2);
    ASSERT_NE(kayit, nullptr);
    EXPECT_EQ(kayit->info.current_task, swarm::TaskType::GO_TO_TARGET);
    EXPECT_EQ(yonetici.peer_count(), 1u);  // aynı peer, ikinci kayıt açılmadı
}

// ---------------------------------------------------------------------------
//  Zaman aşımı
// ---------------------------------------------------------------------------

TEST(PeerManager, TimeoutDolmadanOnlineKalir)
{
    swarm::PeerManager yonetici{3000ms};
    yonetici.on_heartbeat(heartbeat_olustur(1), BASLANGIC);

    yonetici.refresh_status(BASLANGIC + 2999ms);

    EXPECT_EQ(yonetici.status_of(1), swarm::PeerStatus::ONLINE);
}

TEST(PeerManager, TimeoutDolunsaOfflineOlur)
{
    swarm::PeerManager yonetici{3000ms};
    yonetici.on_heartbeat(heartbeat_olustur(1), BASLANGIC);

    yonetici.refresh_status(BASLANGIC + 3000ms);

    EXPECT_EQ(yonetici.status_of(1), swarm::PeerStatus::OFFLINE);
}

// ---------------------------------------------------------------------------
//  Telemetri bayatlık kontrolü
// ---------------------------------------------------------------------------

TEST(PeerManager, TanimadigiPeerinTelemetrisiReddedilir)
{
    swarm::PeerManager yonetici;

    // Heartbeat ile tanışmadığımız bir drone'un telemetrisi tabloya
    // kayıt AÇMAMALI.
    EXPECT_FALSE(yonetici.on_telemetry(telemetri_olustur(9, 1), BASLANGIC));
    EXPECT_EQ(yonetici.peer_count(), 0u);
}

TEST(PeerManager, ArtanSeqNumKabulEdilir)
{
    swarm::PeerManager yonetici;
    yonetici.on_heartbeat(heartbeat_olustur(1), BASLANGIC);

    EXPECT_TRUE(yonetici.on_telemetry(telemetri_olustur(1, 1), BASLANGIC));
    EXPECT_TRUE(yonetici.on_telemetry(telemetri_olustur(1, 2), BASLANGIC));
    EXPECT_TRUE(yonetici.on_telemetry(telemetri_olustur(1, 10), BASLANGIC));

    EXPECT_EQ(yonetici.find(1)->info.last_seen_seq, 10u);
}

TEST(PeerManager, GeriKalanSeqNumReddedilir)
{
    // UDP paket sırasını bozabilir: 10'dan sonra gelen 7 BAYATtır, atılmalı.
    swarm::PeerManager yonetici;
    yonetici.on_heartbeat(heartbeat_olustur(1), BASLANGIC);
    ASSERT_TRUE(yonetici.on_telemetry(telemetri_olustur(1, 10), BASLANGIC));

    EXPECT_FALSE(yonetici.on_telemetry(telemetri_olustur(1, 7), BASLANGIC));

    // Reddedilen paket sayacı geriye çekmemeli.
    EXPECT_EQ(yonetici.find(1)->info.last_seen_seq, 10u);
}

TEST(PeerManager, AyniSeqNumTekrariReddedilir)
{
    swarm::PeerManager yonetici;
    yonetici.on_heartbeat(heartbeat_olustur(1), BASLANGIC);
    ASSERT_TRUE(yonetici.on_telemetry(telemetri_olustur(1, 5), BASLANGIC));

    EXPECT_FALSE(yonetici.on_telemetry(telemetri_olustur(1, 5), BASLANGIC));
}

// ---------------------------------------------------------------------------
//  ASIL SENARYO: OFFLINE -> ONLINE geçişinde last_seen_seq sıfırlanır
// ---------------------------------------------------------------------------

TEST(PeerManager, RestartSonrasiSeqSifirlanirVeTazeVeriKabulEdilir)
{
    swarm::PeerManager yonetici{3000ms};

    // 1) Drone ayakta, telemetri sayacı yükseliyor.
    yonetici.on_heartbeat(heartbeat_olustur(1), BASLANGIC);
    ASSERT_TRUE(yonetici.on_telemetry(telemetri_olustur(1, 5000), BASLANGIC));
    ASSERT_EQ(yonetici.find(1)->info.last_seen_seq, 5000u);

    // 2) Drone susuyor; zaman aşımı dolunca OFFLINE'a düşüyor.
    yonetici.refresh_status(BASLANGIC + 5s);
    ASSERT_EQ(yonetici.status_of(1), swarm::PeerStatus::OFFLINE);

    // 3) Drone yeniden başlıyor ve heartbeat yolluyor.
    //    OFFLINE -> ONLINE geçişi burada olur ve sayaç sıfırlanır.
    yonetici.on_heartbeat(heartbeat_olustur(1), BASLANGIC + 6s);

    EXPECT_EQ(yonetici.status_of(1), swarm::PeerStatus::ONLINE);
    EXPECT_EQ(yonetici.find(1)->info.last_seen_seq, 0u);

    // 4) Restart sonrası taze telemetri 1'den başlıyor. Sayaç sıfırlanmamış
    //    olsaydı bu paket "5000'den küçük" diye reddedilir ve drone bir daha
    //    hiç görünmezdi. İşte bu testin koruduğu hata budur.
    EXPECT_TRUE(yonetici.on_telemetry(telemetri_olustur(1, 1), BASLANGIC + 6s));
    EXPECT_EQ(yonetici.find(1)->info.last_seen_seq, 1u);
}

TEST(PeerManager, OnlineKalanPeerinSeqSayaciSifirlanmaz)
{
    // Ters kontrol: peer hiç OFFLINE'a düşmediyse, gelen her heartbeat
    // sayacı sıfırlamamalı. Aksi halde her heartbeat bayatlık korumasını
    // silerdi.
    swarm::PeerManager yonetici{3000ms};

    yonetici.on_heartbeat(heartbeat_olustur(1), BASLANGIC);
    ASSERT_TRUE(yonetici.on_telemetry(telemetri_olustur(1, 100), BASLANGIC));

    yonetici.on_heartbeat(heartbeat_olustur(1), BASLANGIC + 1s);

    EXPECT_EQ(yonetici.find(1)->info.last_seen_seq, 100u);
}

TEST(PeerManager, BirPeerinOfflineOlmasiDigeriniEtkilemez)
{
    swarm::PeerManager yonetici{3000ms};

    yonetici.on_heartbeat(heartbeat_olustur(1), BASLANGIC);
    yonetici.on_heartbeat(heartbeat_olustur(2), BASLANGIC);
    ASSERT_TRUE(yonetici.on_telemetry(telemetri_olustur(2, 50), BASLANGIC));

    // Sadece 2 numara heartbeat yollamaya devam ediyor.
    yonetici.on_heartbeat(heartbeat_olustur(2), BASLANGIC + 2s);
    yonetici.refresh_status(BASLANGIC + 4s);

    EXPECT_EQ(yonetici.status_of(1), swarm::PeerStatus::OFFLINE);
    EXPECT_EQ(yonetici.status_of(2), swarm::PeerStatus::ONLINE);
    EXPECT_EQ(yonetici.find(2)->info.last_seen_seq, 50u);
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
    swarm::PeerManager yonetici{3000ms};
    yonetici.on_heartbeat(heartbeat_olustur(1), BASLANGIC);

    // Sayaç epeyce ilerlemiş.
    ASSERT_TRUE(yonetici.on_telemetry(telemetri_olustur(1, 5000), BASLANGIC));

    // Drone 1 saniyede yeniden başlıyor: OFFLINE'a hiç düşmüyor.
    yonetici.on_heartbeat(heartbeat_olustur(1), BASLANGIC + 1s);
    ASSERT_EQ(yonetici.status_of(1), swarm::PeerStatus::ONLINE);
    ASSERT_EQ(yonetici.find(1)->info.last_seen_seq, 5000u)
            << "OFFLINE'a dusmedigi icin sayac sifirlanmamis olmali";

    // Restart sonrası taze telemetri 1'den başlıyor. Büyük geri sıçrama
    // tespit edilip takip sıfırlanmalı ve paket KABUL edilmeli.
    bool yeni_akis = false;
    EXPECT_TRUE(yonetici.on_telemetry(telemetri_olustur(1, 1), BASLANGIC + 1s, &yeni_akis));
    EXPECT_TRUE(yeni_akis);
    EXPECT_EQ(yonetici.find(1)->info.last_seen_seq, 1u);
}

TEST(PeerManager, KucukGeriSicramaHalaBayatSayilir)
{
    // Restart koruması, asıl bayatlık korumasını ZAYIFLATMAMALI. UDP'de
    // sıra bozulması birkaç paketliktir; böyle bir paket hâlâ atılmalı.
    swarm::PeerManager yonetici{3000ms};
    yonetici.on_heartbeat(heartbeat_olustur(1), BASLANGIC);
    ASSERT_TRUE(yonetici.on_telemetry(telemetri_olustur(1, 5000), BASLANGIC));

    EXPECT_FALSE(yonetici.on_telemetry(telemetri_olustur(1, 4990), BASLANGIC));
    EXPECT_EQ(yonetici.find(1)->info.last_seen_seq, 5000u);
}

TEST(PeerManager, RestartEsigiSinirindaDavranis)
{
    swarm::PeerManager yonetici{3000ms};
    yonetici.on_heartbeat(heartbeat_olustur(1), BASLANGIC);
    ASSERT_TRUE(yonetici.on_telemetry(telemetri_olustur(1, 1000), BASLANGIC));

    // Eşik kadar geri: henüz restart sayılmaz, bayat kabul edilir.
    const uint32_t esik = swarm::PeerManager::RESTART_TESPIT_ESIGI;
    EXPECT_FALSE(yonetici.on_telemetry(telemetri_olustur(1, 1000 - esik), BASLANGIC));

    // Eşiğin bir fazlası kadar geri: restart sayılır, kabul edilir.
    EXPECT_TRUE(yonetici.on_telemetry(telemetri_olustur(1, 1000 - esik - 1), BASLANGIC));
}

TEST(PeerManager, IlkTelemetriYeniAkisOlarakIsaretlenir)
{
    swarm::PeerManager yonetici;
    yonetici.on_heartbeat(heartbeat_olustur(1), BASLANGIC);

    bool yeni_akis = false;
    EXPECT_TRUE(yonetici.on_telemetry(telemetri_olustur(1, 1), BASLANGIC, &yeni_akis));
    EXPECT_TRUE(yeni_akis);

    // İkinci paket yeni akış değil.
    EXPECT_TRUE(yonetici.on_telemetry(telemetri_olustur(1, 2), BASLANGIC, &yeni_akis));
    EXPECT_FALSE(yeni_akis);
}
