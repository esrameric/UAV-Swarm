// ============================================================================
//  Faz 2.2 — 9 child task'ın testi
//
//  Her task için asgari iki şey doğrulanıyor (plan gereği): get_type() doğru
//  tipi bildiriyor mu, is_finished() doğru davranıyor mu. Buna ek olarak her
//  task'ın kendi asıl davranışı da sınanıyor.
//
//  Testler aynı zamanda bu task'ların NASIL KULLANILDIĞINI gösteren
//  örneklerdir: kur -> on_enter -> run -> is_finished.
// ============================================================================

#include <gtest/gtest.h>

#include "swarm/task/consensus_task.hpp"
#include "swarm/task/discovery_task.hpp"
#include "swarm/task/fail_safe_task.hpp"
#include "swarm/task/go_to_target_task.hpp"
#include "swarm/task/hover_task.hpp"
#include "swarm/task/idle_task.hpp"
#include "swarm/task/init_task.hpp"
#include "swarm/task/landing_task.hpp"
#include "swarm/task/motion.hpp"
#include "swarm/task/scout_search_task.hpp"

#include <chrono>

namespace {

using namespace std::chrono_literals;

const swarm::TimePoint BASLANGIC{};

swarm::Heartbeat heartbeat_olustur(uint8_t drone_id)
{
    swarm::Heartbeat kalp_atisi;
    kalp_atisi.drone_id(drone_id);
    kalp_atisi.node_type(swarm::NodeType::DRONE);
    return kalp_atisi;
}

}  // namespace

// ---------------------------------------------------------------------------
//  Hareket yardımcısı
// ---------------------------------------------------------------------------

TEST(Motion, YatayMesafeDogruHesaplanir)
{
    // 3-4-5 üçgeni
    EXPECT_DOUBLE_EQ(swarm::yatay_mesafe(0.0, 0.0, 3.0, 4.0), 5.0);
    EXPECT_DOUBLE_EQ(swarm::yatay_mesafe(1.0, 1.0, 1.0, 1.0), 0.0);
}

TEST(Motion, HedefeDogruIlerletmeSabitHizlaCalisir)
{
    swarm::DroneState durum;
    // 10 m/s hızla 1 saniye -> 10 metre ilerlemeli (hedef 100 m uzakta).
    const bool vardi = swarm::hedefe_dogru_ilerlet(durum, 100.0, 0.0, 10.0, 0.5, 1.0);

    EXPECT_FALSE(vardi);
    EXPECT_DOUBLE_EQ(durum.x, 10.0);
    EXPECT_DOUBLE_EQ(durum.vx, 10.0);
}

TEST(Motion, HedefiAsmakYerineTamUstuneOturur)
{
    // Hedef 2 metre uzakta ama bir adımda 10 metre gidiyoruz: araç hedefi
    // geçmemeli, tam üstüne oturmalı. Aksi halde etrafında salınırdı.
    swarm::DroneState durum;
    const bool vardi = swarm::hedefe_dogru_ilerlet(durum, 2.0, 0.0, 10.0, 0.1, 1.0);

    EXPECT_TRUE(vardi);
    EXPECT_DOUBLE_EQ(durum.x, 2.0);
    EXPECT_DOUBLE_EQ(durum.vx, 0.0);
}

// ---------------------------------------------------------------------------
//  InitTask
// ---------------------------------------------------------------------------

TEST(InitTask, TipiVeIlkTurdaBitmesi)
{
    swarm::InitTask gorev;

    EXPECT_EQ(gorev.get_type(), swarm::TaskType::INIT);

    gorev.on_enter(BASLANGIC);
    EXPECT_FALSE(gorev.is_finished());

    gorev.run(BASLANGIC);
    EXPECT_TRUE(gorev.is_finished());
}

// ---------------------------------------------------------------------------
//  IdleTask
// ---------------------------------------------------------------------------

TEST(IdleTask, TipiVeAsalaBitmemesi)
{
    swarm::DroneState durum;
    swarm::IdleTask gorev{durum};

    EXPECT_EQ(gorev.get_type(), swarm::TaskType::IDLE);

    gorev.on_enter(BASLANGIC);
    for (int tur = 0; tur < 100; ++tur)
    {
        gorev.run(BASLANGIC + std::chrono::seconds(tur));
        // IdleTask kendiliğinden ASLA bitmez; kuyruğun varsayılan hâlidir.
        ASSERT_FALSE(gorev.is_finished());
    }
}

TEST(IdleTask, HareketiDurdurur)
{
    swarm::DroneState durum;
    durum.vx = 5.0;
    durum.vy = -3.0;

    swarm::IdleTask gorev{durum};
    gorev.on_enter(BASLANGIC);

    EXPECT_DOUBLE_EQ(durum.vx, 0.0);
    EXPECT_DOUBLE_EQ(durum.vy, 0.0);
}

// ---------------------------------------------------------------------------
//  HoverTask
// ---------------------------------------------------------------------------

TEST(HoverTask, TipiVeSureDolunlaBitmesi)
{
    swarm::DroneState durum;
    swarm::HoverTask gorev{durum, 2000ms};

    EXPECT_EQ(gorev.get_type(), swarm::TaskType::HOVER);

    gorev.on_enter(BASLANGIC);
    gorev.run(BASLANGIC + 1999ms);
    EXPECT_FALSE(gorev.is_finished());

    gorev.run(BASLANGIC + 2000ms);
    EXPECT_TRUE(gorev.is_finished());
}

// ---------------------------------------------------------------------------
//  FailSafeTask
// ---------------------------------------------------------------------------

TEST(FailSafeTask, TipiVeAninaDurdurmasi)
{
    swarm::DroneState durum;
    durum.vx = 12.0;
    durum.vz = -4.0;

    swarm::FailSafeTask gorev{durum, 1000ms};

    EXPECT_EQ(gorev.get_type(), swarm::TaskType::FAIL_SAFE);

    // Acil durumda ilk iş: hareketi anında kes (on_enter'da).
    gorev.on_enter(BASLANGIC);
    EXPECT_DOUBLE_EQ(durum.vx, 0.0);
    EXPECT_DOUBLE_EQ(durum.vz, 0.0);
    EXPECT_FALSE(gorev.is_finished());
}

TEST(FailSafeTask, DegerlendirmeSuresiSonundaBiter)
{
    swarm::DroneState durum;
    swarm::FailSafeTask gorev{durum, 1000ms};

    gorev.on_enter(BASLANGIC);
    gorev.run(BASLANGIC + 999ms);
    EXPECT_FALSE(gorev.is_finished());

    gorev.run(BASLANGIC + 1000ms);
    EXPECT_TRUE(gorev.is_finished());
}

// ---------------------------------------------------------------------------
//  LandingTask
// ---------------------------------------------------------------------------

TEST(LandingTask, TipiVeYereDeginceBitmesi)
{
    swarm::DroneState durum;
    durum.z = 3.0;  // 3 metre yükseklikte

    swarm::LandingTask gorev{durum};

    EXPECT_EQ(gorev.get_type(), swarm::TaskType::LANDING);

    gorev.on_enter(BASLANGIC);

    // 1 m/s iniş hızıyla 1 saniye -> 2 metre kaldı
    gorev.run(BASLANGIC + 1s);
    EXPECT_DOUBLE_EQ(durum.z, 2.0);
    EXPECT_DOUBLE_EQ(durum.vz, -1.0);
    EXPECT_FALSE(gorev.is_finished());

    // 2 saniye daha -> yere değdi
    gorev.run(BASLANGIC + 3s);
    EXPECT_DOUBLE_EQ(durum.z, 0.0);
    EXPECT_TRUE(gorev.is_finished());
}

TEST(LandingTask, YerdeBaslarsaAninaBiter)
{
    swarm::DroneState durum;  // z = 0
    swarm::LandingTask gorev{durum};

    gorev.on_enter(BASLANGIC);
    gorev.run(BASLANGIC);

    EXPECT_TRUE(gorev.is_finished());
    EXPECT_DOUBLE_EQ(durum.z, 0.0);
}

// ---------------------------------------------------------------------------
//  GoToTargetTask  (STRIKER görevi)
// ---------------------------------------------------------------------------

TEST(GoToTargetTask, TipiVeHedefeVarinaBitmesi)
{
    swarm::DroneState durum;
    swarm::GoToTargetTask gorev{durum, 50.0, 0.0};

    EXPECT_EQ(gorev.get_type(), swarm::TaskType::GO_TO_TARGET);
    EXPECT_DOUBLE_EQ(gorev.hedef_x(), 50.0);

    gorev.on_enter(BASLANGIC);

    // 5 m/s ile 1 saniye -> 5 metre
    gorev.run(BASLANGIC + 1s);
    EXPECT_DOUBLE_EQ(durum.x, 5.0);
    EXPECT_FALSE(gorev.is_finished());

    // 50 metre için toplam 10 saniye yeter
    gorev.run(BASLANGIC + 10s);
    EXPECT_DOUBLE_EQ(durum.x, 50.0);
    EXPECT_TRUE(gorev.is_finished());
}

TEST(GoToTargetTask, HedeftekiDroneAninaBitirir)
{
    swarm::DroneState durum;
    durum.x = 20.0;
    durum.y = 20.0;

    swarm::GoToTargetTask gorev{durum, 20.0, 20.0};
    gorev.on_enter(BASLANGIC);
    gorev.run(BASLANGIC);

    EXPECT_TRUE(gorev.is_finished());
}

// ---------------------------------------------------------------------------
//  ScoutSearchTask  (SCOUT görevi)
// ---------------------------------------------------------------------------

TEST(ScoutSearchTask, TipiVeIkiAsamaliAkis)
{
    swarm::DroneState durum;
    swarm::ScoutSearchTask gorev{durum, 25.0, 0.0, 2000ms};

    EXPECT_EQ(gorev.get_type(), swarm::TaskType::SCOUT_SEARCH);

    gorev.on_enter(BASLANGIC);

    // 1. aşama: bölgeye uçuş (25 m / 5 m/s = 5 sn)
    gorev.run(BASLANGIC + 1s);
    EXPECT_FALSE(gorev.bolgeye_varildi());
    EXPECT_FALSE(gorev.is_finished());

    gorev.run(BASLANGIC + 5s);
    EXPECT_TRUE(gorev.bolgeye_varildi());
    EXPECT_DOUBLE_EQ(durum.x, 25.0);
    // Bölgeye varmak görevi bitirmez; tarama daha yeni başlıyor.
    EXPECT_FALSE(gorev.is_finished());

    // 2. aşama: tarama (2 sn)
    gorev.run(BASLANGIC + 6s);
    EXPECT_FALSE(gorev.is_finished());

    gorev.run(BASLANGIC + 7s);
    EXPECT_TRUE(gorev.is_finished());
}

TEST(ScoutSearchTask, TaramaSayaciVaristanSonraBaslar)
{
    // Uzak bir bölgeye uçuş süresi taramadan sayılmamalı.
    swarm::DroneState durum;
    swarm::ScoutSearchTask gorev{durum, 100.0, 0.0, 1000ms};

    gorev.on_enter(BASLANGIC);
    gorev.run(BASLANGIC + 20s);   // 100 m / 5 m/s = 20 sn -> tam varış

    ASSERT_TRUE(gorev.bolgeye_varildi());
    // Uçuş 20 saniye sürdü ama tarama süresi (1 sn) daha yeni başladı.
    EXPECT_FALSE(gorev.is_finished());
}

// ---------------------------------------------------------------------------
//  DiscoveryTask
// ---------------------------------------------------------------------------

TEST(DiscoveryTask, TipiVePeerBulununcaBitmesi)
{
    swarm::PeerManager peer_yoneticisi;
    swarm::DiscoveryTask gorev{peer_yoneticisi, 5000ms};

    EXPECT_EQ(gorev.get_type(), swarm::TaskType::DISCOVERY);

    gorev.on_enter(BASLANGIC);
    gorev.run(BASLANGIC + 1s);
    EXPECT_FALSE(gorev.is_finished());

    // Bir peer duyuldu -> keşif tamamlanır (3 drone'un hepsi beklenmez).
    peer_yoneticisi.on_heartbeat(heartbeat_olustur(2), BASLANGIC + 2s);

    gorev.run(BASLANGIC + 2s);
    EXPECT_TRUE(gorev.is_finished());
    EXPECT_TRUE(gorev.peer_bulundu());
}

TEST(DiscoveryTask, HicKimseYoksaSureSonundaYineDeBiter)
{
    // NON-BLOCKING KEŞİF (Bölüm 2): gerçek sahada bir drone hiç ayağa
    // kalkmayabilir; sistem buna takılıp kalmamalı.
    swarm::PeerManager peer_yoneticisi;
    swarm::DiscoveryTask gorev{peer_yoneticisi, 5000ms};

    gorev.on_enter(BASLANGIC);
    gorev.run(BASLANGIC + 4999ms);
    EXPECT_FALSE(gorev.is_finished());

    gorev.run(BASLANGIC + 5000ms);
    EXPECT_TRUE(gorev.is_finished());
    EXPECT_FALSE(gorev.peer_bulundu());  // süre dolduğu için bitti
}

// ---------------------------------------------------------------------------
//  ConsensusTask
// ---------------------------------------------------------------------------

TEST(ConsensusTask, TipiVeBaslangictaPending)
{
    swarm::ConsensusTask gorev{1, {1, 2, 3}};

    EXPECT_EQ(gorev.get_type(), swarm::TaskType::CONSENSUS);
    EXPECT_EQ(gorev.transaction_id(), 1u);

    gorev.on_enter(BASLANGIC);
    EXPECT_EQ(gorev.result(), swarm::ConsensusResult::PENDING);
    EXPECT_FALSE(gorev.is_finished());
    EXPECT_EQ(gorev.oy_durumu(1), swarm::Vote::PENDING);
}

TEST(ConsensusTask, HerkesAckVerinceCommitted)
{
    swarm::ConsensusTask gorev{7, {1, 2, 3}};
    gorev.on_enter(BASLANGIC);

    gorev.on_vote(1, swarm::Vote::ACK);
    EXPECT_FALSE(gorev.is_finished());

    gorev.on_vote(2, swarm::Vote::ACK);
    EXPECT_FALSE(gorev.is_finished());

    gorev.on_vote(3, swarm::Vote::ACK);
    EXPECT_TRUE(gorev.is_finished());
    EXPECT_EQ(gorev.result(), swarm::ConsensusResult::COMMITTED);
}

TEST(ConsensusTask, TekNackAninaAbortEder)
{
    // 2PC'de karar oybirliği gerektirir: bir NACK yeterlidir, diğerlerini
    // beklemeye gerek yoktur.
    swarm::ConsensusTask gorev{7, {1, 2, 3}};
    gorev.on_enter(BASLANGIC);

    gorev.on_vote(1, swarm::Vote::ACK);
    gorev.on_vote(2, swarm::Vote::NACK);

    EXPECT_TRUE(gorev.is_finished());
    EXPECT_EQ(gorev.result(), swarm::ConsensusResult::ABORTED);
}

TEST(ConsensusTask, BeklenmeyenGondericininOyuYokSayilir)
{
    swarm::ConsensusTask gorev{7, {1, 2}};
    gorev.on_enter(BASLANGIC);

    gorev.on_vote(99, swarm::Vote::NACK);  // listede olmayan düğüm

    EXPECT_FALSE(gorev.is_finished());
    EXPECT_EQ(gorev.result(), swarm::ConsensusResult::PENDING);
}

TEST(ConsensusTask, SonucBelliOlduktanSonraGelenOylarKarariDegistirmez)
{
    swarm::ConsensusTask gorev{7, {1, 2}};
    gorev.on_enter(BASLANGIC);

    gorev.on_vote(1, swarm::Vote::NACK);
    ASSERT_EQ(gorev.result(), swarm::ConsensusResult::ABORTED);

    gorev.on_vote(2, swarm::Vote::ACK);

    EXPECT_EQ(gorev.result(), swarm::ConsensusResult::ABORTED);
}

TEST(ConsensusTask, OyVerecekKimseYoksaAninaCommitted)
{
    // Tek başına kalmış bir düğüm oylamada sonsuza kadar kilitlenmemeli.
    swarm::ConsensusTask gorev{7, {}};

    gorev.on_enter(BASLANGIC);

    EXPECT_TRUE(gorev.is_finished());
    EXPECT_EQ(gorev.result(), swarm::ConsensusResult::COMMITTED);
}

// ---------------------------------------------------------------------------
//  Faz 2.3 — ConsensusTask zaman aşımı ve görev iptali
// ---------------------------------------------------------------------------

TEST(ConsensusTaskTimeout, VarsayilanSureBesSaniye)
{
    // Bölüm 3.6'da kararlaştırılan süre. Sabit yanlışlıkla değiştirilirse
    // bu test uyarır.
    EXPECT_EQ(swarm::ConsensusTask::VARSAYILAN_TIMEOUT, 5000ms);
}

TEST(ConsensusTaskTimeout, SureDolmadanOylamaAcikKalir)
{
    swarm::ConsensusTask gorev{1, {1, 2, 3}, 5000ms};
    gorev.on_enter(BASLANGIC);

    gorev.run(BASLANGIC + 4999ms);

    EXPECT_FALSE(gorev.is_finished());
    EXPECT_EQ(gorev.result(), swarm::ConsensusResult::PENDING);
}

TEST(ConsensusTaskTimeout, BesSaniyeDolunlaGorevIptalEdilir)
{
    swarm::ConsensusTask gorev{1, {1, 2, 3}, 5000ms};
    gorev.on_enter(BASLANGIC);

    // Sadece iki drone cevap verdi; üçüncüsü hiç ses çıkarmadı (PENDING).
    gorev.on_vote(1, swarm::Vote::ACK);
    gorev.on_vote(2, swarm::Vote::ACK);
    ASSERT_FALSE(gorev.is_finished());

    gorev.run(BASLANGIC + 5000ms);

    EXPECT_TRUE(gorev.is_finished());
    EXPECT_EQ(gorev.result(), swarm::ConsensusResult::ABORTED);
    EXPECT_TRUE(gorev.timeout_ile_iptal_oldu());

    // Cevap vermeyen düğüm PENDING'de kalmış olmalı — "cevap yok" ile
    // "açık NACK" birbirine karıştırılmıyor.
    EXPECT_EQ(gorev.oy_durumu(3), swarm::Vote::PENDING);
}

TEST(ConsensusTaskTimeout, IptalDurumundaTumGorevIptalTalimatiVerilir)
{
    // Bölüm 2: timeout sonrası yalnızca bu task bitmez, TÜM görev iptal
    // edilir ve sürü IdleTask'a döner. Kuyruğu boşaltmak Task Engine'in
    // işidir; ConsensusTask bunu mission_should_abort() ile bildirir.
    swarm::ConsensusTask gorev{1, {1, 2}, 5000ms};
    gorev.on_enter(BASLANGIC);

    EXPECT_FALSE(gorev.mission_should_abort());

    gorev.run(BASLANGIC + 5s);

    EXPECT_TRUE(gorev.mission_should_abort());
}

TEST(ConsensusTaskTimeout, SureDolmadanHerkesAckVerirseCommitEdilir)
{
    swarm::ConsensusTask gorev{1, {1, 2}, 5000ms};
    gorev.on_enter(BASLANGIC);

    gorev.on_vote(1, swarm::Vote::ACK);
    gorev.on_vote(2, swarm::Vote::ACK);

    // Süre dolsa bile sonuç değişmez: karar zaten verildi.
    gorev.run(BASLANGIC + 10s);

    EXPECT_EQ(gorev.result(), swarm::ConsensusResult::COMMITTED);
    EXPECT_FALSE(gorev.mission_should_abort());
    EXPECT_FALSE(gorev.timeout_ile_iptal_oldu());
}

TEST(ConsensusTaskTimeout, NackIleIptalTimeoutIleIptalDenSayilmaz)
{
    // İkisi de ABORTED ile biter ama sebepleri farklıdır; log ve teşhis
    // için ayırt edilebilmeli.
    swarm::ConsensusTask gorev{1, {1, 2}, 5000ms};
    gorev.on_enter(BASLANGIC);

    gorev.on_vote(1, swarm::Vote::NACK);

    EXPECT_EQ(gorev.result(), swarm::ConsensusResult::ABORTED);
    EXPECT_TRUE(gorev.mission_should_abort());
    EXPECT_FALSE(gorev.timeout_ile_iptal_oldu());
}

TEST(ConsensusTaskTimeout, SureDolduktanSonraGelenAckKarariDegistirmez)
{
    swarm::ConsensusTask gorev{1, {1}, 5000ms};
    gorev.on_enter(BASLANGIC);

    gorev.run(BASLANGIC + 5s);
    ASSERT_EQ(gorev.result(), swarm::ConsensusResult::ABORTED);

    // Geç kalan oy kabul edilmemeli: görev çoktan iptal edildi.
    gorev.on_vote(1, swarm::Vote::ACK);

    EXPECT_EQ(gorev.result(), swarm::ConsensusResult::ABORTED);
}
