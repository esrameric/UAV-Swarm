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

const swarm::TimePoint START{};

swarm::Heartbeat create_heartbeat(uint8_t drone_id)
{
    swarm::Heartbeat heartbeat;
    heartbeat.drone_id(drone_id);
    heartbeat.node_type(swarm::NodeType::DRONE);
    return heartbeat;
}

}  // namespace

// ---------------------------------------------------------------------------
//  Hareket yardımcısı
// ---------------------------------------------------------------------------

TEST(Motion, YatayMesafeDogruHesaplanir)
{
    // 3-4-5 üçgeni
    EXPECT_DOUBLE_EQ(swarm::horizontal_distance(0.0, 0.0, 3.0, 4.0), 5.0);
    EXPECT_DOUBLE_EQ(swarm::horizontal_distance(1.0, 1.0, 1.0, 1.0), 0.0);
}

TEST(Motion, HedefeDogruIlerletmeSabitHizlaCalisir)
{
    swarm::DroneState state;
    // 10 m/s hızla 1 saniye -> 10 metre ilerlemeli (hedef 100 m uzakta).
    const bool arrived = swarm::move_toward_target(state, 100.0, 0.0, 10.0, 0.5, 1.0);

    EXPECT_FALSE(arrived);
    EXPECT_DOUBLE_EQ(state.x, 10.0);
    EXPECT_DOUBLE_EQ(state.vx, 10.0);
}

TEST(Motion, HedefiAsmakYerineTamUstuneOturur)
{
    // Hedef 2 metre uzakta ama bir adımda 10 metre gidiyoruz: araç hedefi
    // geçmemeli, tam üstüne oturmalı. Aksi halde etrafında salınırdı.
    swarm::DroneState state;
    const bool arrived = swarm::move_toward_target(state, 2.0, 0.0, 10.0, 0.1, 1.0);

    EXPECT_TRUE(arrived);
    EXPECT_DOUBLE_EQ(state.x, 2.0);
    EXPECT_DOUBLE_EQ(state.vx, 0.0);
}

// ---------------------------------------------------------------------------
//  InitTask
// ---------------------------------------------------------------------------

TEST(InitTask, TipiVeIlkTurdaBitmesi)
{
    swarm::InitTask task;

    EXPECT_EQ(task.get_type(), swarm::TaskType::INIT);

    task.on_enter(START);
    EXPECT_FALSE(task.is_finished());

    task.run(START);
    EXPECT_TRUE(task.is_finished());
}

// ---------------------------------------------------------------------------
//  IdleTask
// ---------------------------------------------------------------------------

TEST(IdleTask, TipiVeAsalaBitmemesi)
{
    swarm::DroneState state;
    swarm::IdleTask task{state};

    EXPECT_EQ(task.get_type(), swarm::TaskType::IDLE);

    task.on_enter(START);
    for (int round = 0; round < 100; ++round)
    {
        task.run(START + std::chrono::seconds(round));
        // IdleTask kendiliğinden ASLA bitmez; queue'nun varsayılan hâlidir.
        ASSERT_FALSE(task.is_finished());
    }
}

TEST(IdleTask, HareketiDurdurur)
{
    swarm::DroneState state;
    state.vx = 5.0;
    state.vy = -3.0;

    swarm::IdleTask task{state};
    task.on_enter(START);

    EXPECT_DOUBLE_EQ(state.vx, 0.0);
    EXPECT_DOUBLE_EQ(state.vy, 0.0);
}

// ---------------------------------------------------------------------------
//  HoverTask
// ---------------------------------------------------------------------------

TEST(HoverTask, TipiVeSureDolunlaBitmesi)
{
    swarm::DroneState state;
    swarm::HoverTask task{state, 2000ms};

    EXPECT_EQ(task.get_type(), swarm::TaskType::HOVER);

    task.on_enter(START);
    task.run(START + 1999ms);
    EXPECT_FALSE(task.is_finished());

    task.run(START + 2000ms);
    EXPECT_TRUE(task.is_finished());
}

// ---------------------------------------------------------------------------
//  FailSafeTask
// ---------------------------------------------------------------------------

TEST(FailSafeTask, TipiVeAninaDurdurmasi)
{
    swarm::DroneState state;
    state.vx = 12.0;
    state.vz = -4.0;

    swarm::FailSafeTask task{state, 1000ms};

    EXPECT_EQ(task.get_type(), swarm::TaskType::FAIL_SAFE);

    // Acil durumda ilk iş: hareketi anında kes (on_enter'da).
    task.on_enter(START);
    EXPECT_DOUBLE_EQ(state.vx, 0.0);
    EXPECT_DOUBLE_EQ(state.vz, 0.0);
    EXPECT_FALSE(task.is_finished());
}

TEST(FailSafeTask, DegerlendirmeSuresiSonundaBiter)
{
    swarm::DroneState state;
    swarm::FailSafeTask task{state, 1000ms};

    task.on_enter(START);
    task.run(START + 999ms);
    EXPECT_FALSE(task.is_finished());

    task.run(START + 1000ms);
    EXPECT_TRUE(task.is_finished());
}

// ---------------------------------------------------------------------------
//  LandingTask
// ---------------------------------------------------------------------------

TEST(LandingTask, TipiVeYereDeginceBitmesi)
{
    swarm::DroneState state;
    state.z = 3.0;  // 3 metre yükseklikte

    swarm::LandingTask task{state};

    EXPECT_EQ(task.get_type(), swarm::TaskType::LANDING);

    task.on_enter(START);

    // 1 m/s iniş hızıyla 1 saniye -> 2 metre kaldı
    task.run(START + 1s);
    EXPECT_DOUBLE_EQ(state.z, 2.0);
    EXPECT_DOUBLE_EQ(state.vz, -1.0);
    EXPECT_FALSE(task.is_finished());

    // 2 saniye daha -> yere değdi
    task.run(START + 3s);
    EXPECT_DOUBLE_EQ(state.z, 0.0);
    EXPECT_TRUE(task.is_finished());
}

TEST(LandingTask, YerdeBaslarsaAninaBiter)
{
    swarm::DroneState state;  // z = 0
    swarm::LandingTask task{state};

    task.on_enter(START);
    task.run(START);

    EXPECT_TRUE(task.is_finished());
    EXPECT_DOUBLE_EQ(state.z, 0.0);
}

// ---------------------------------------------------------------------------
//  GoToTargetTask  (STRIKER görevi)
// ---------------------------------------------------------------------------

TEST(GoToTargetTask, TipiVeHedefeVarinaBitmesi)
{
    swarm::DroneState state;
    swarm::GoToTargetTask task{state, 50.0, 0.0};

    EXPECT_EQ(task.get_type(), swarm::TaskType::GO_TO_TARGET);
    EXPECT_DOUBLE_EQ(task.target_x(), 50.0);

    task.on_enter(START);

    // 5 m/s ile 1 saniye -> 5 metre
    task.run(START + 1s);
    EXPECT_DOUBLE_EQ(state.x, 5.0);
    EXPECT_FALSE(task.is_finished());

    // 50 metre için toplam 10 saniye yeter
    task.run(START + 10s);
    EXPECT_DOUBLE_EQ(state.x, 50.0);
    EXPECT_TRUE(task.is_finished());
}

TEST(GoToTargetTask, HedeftekiDroneAninaBitirir)
{
    swarm::DroneState state;
    state.x = 20.0;
    state.y = 20.0;

    swarm::GoToTargetTask task{state, 20.0, 20.0};
    task.on_enter(START);
    task.run(START);

    EXPECT_TRUE(task.is_finished());
}

// ---------------------------------------------------------------------------
//  ScoutSearchTask  (SCOUT görevi)
// ---------------------------------------------------------------------------

TEST(ScoutSearchTask, TipiVeIkiAsamaliAkis)
{
    swarm::DroneState state;
    swarm::ScoutSearchTask task{state, 25.0, 0.0, 2000ms};

    EXPECT_EQ(task.get_type(), swarm::TaskType::SCOUT_SEARCH);

    task.on_enter(START);

    // 1. aşama: bölgeye uçuş (25 m / 5 m/s = 5 sn)
    task.run(START + 1s);
    EXPECT_FALSE(task.region_reached());
    EXPECT_FALSE(task.is_finished());

    task.run(START + 5s);
    EXPECT_TRUE(task.region_reached());
    EXPECT_DOUBLE_EQ(state.x, 25.0);
    // Bölgeye varmak görevi bitirmez; tarama daha yeni başlıyor.
    EXPECT_FALSE(task.is_finished());

    // 2. aşama: tarama (2 sn)
    task.run(START + 6s);
    EXPECT_FALSE(task.is_finished());

    task.run(START + 7s);
    EXPECT_TRUE(task.is_finished());
}

TEST(ScoutSearchTask, TaramaSayaciVaristanSonraBaslar)
{
    // Uzak bir bölgeye uçuş süresi taramadan sayılmamalı.
    swarm::DroneState state;
    swarm::ScoutSearchTask task{state, 100.0, 0.0, 1000ms};

    task.on_enter(START);
    task.run(START + 20s);   // 100 m / 5 m/s = 20 sn -> tam varış

    ASSERT_TRUE(task.region_reached());
    // Uçuş 20 saniye sürdü ama tarama süresi (1 sn) daha yeni başladı.
    EXPECT_FALSE(task.is_finished());
}

// ---------------------------------------------------------------------------
//  DiscoveryTask
// ---------------------------------------------------------------------------

TEST(DiscoveryTask, TipiVePeerBulununcaBitmesi)
{
    swarm::PeerManager peer_manager;
    swarm::DiscoveryTask task{peer_manager, 5000ms};

    EXPECT_EQ(task.get_type(), swarm::TaskType::DISCOVERY);

    task.on_enter(START);
    task.run(START + 1s);
    EXPECT_FALSE(task.is_finished());

    // Bir peer duyuldu -> keşif tamamlanır (3 drone'un hepsi beklenmez).
    peer_manager.on_heartbeat(create_heartbeat(2), START + 2s);

    task.run(START + 2s);
    EXPECT_TRUE(task.is_finished());
    EXPECT_TRUE(task.peer_found());
}

TEST(DiscoveryTask, HicKimseYoksaSureSonundaYineDeBiter)
{
    // NON-BLOCKING KEŞİF (Bölüm 2): gerçek sahada bir drone hiç ayağa
    // kalkmayabilir; sistem buna takılıp kalmamalı.
    swarm::PeerManager peer_manager;
    swarm::DiscoveryTask task{peer_manager, 5000ms};

    task.on_enter(START);
    task.run(START + 4999ms);
    EXPECT_FALSE(task.is_finished());

    task.run(START + 5000ms);
    EXPECT_TRUE(task.is_finished());
    EXPECT_FALSE(task.peer_found());  // süre dolduğu için bitti
}

// ---------------------------------------------------------------------------
//  ConsensusTask
// ---------------------------------------------------------------------------

TEST(ConsensusTask, TipiVeBaslangictaPending)
{
    swarm::ConsensusTask task{1, {1, 2, 3}};

    EXPECT_EQ(task.get_type(), swarm::TaskType::CONSENSUS);
    EXPECT_EQ(task.transaction_id(), 1u);

    task.on_enter(START);
    EXPECT_EQ(task.result(), swarm::ConsensusResult::PENDING);
    EXPECT_FALSE(task.is_finished());
    EXPECT_EQ(task.vote_status(1), swarm::Vote::PENDING);
}

TEST(ConsensusTask, HerkesAckVerinceCommitted)
{
    swarm::ConsensusTask task{7, {1, 2, 3}};
    task.on_enter(START);

    task.on_vote(1, swarm::Vote::ACK);
    EXPECT_FALSE(task.is_finished());

    task.on_vote(2, swarm::Vote::ACK);
    EXPECT_FALSE(task.is_finished());

    task.on_vote(3, swarm::Vote::ACK);
    EXPECT_TRUE(task.is_finished());
    EXPECT_EQ(task.result(), swarm::ConsensusResult::COMMITTED);
}

TEST(ConsensusTask, TekNackAninaAbortEder)
{
    // 2PC'de karar oybirliği gerektirir: bir NACK yeterlidir, diğerlerini
    // beklemeye gerek yoktur.
    swarm::ConsensusTask task{7, {1, 2, 3}};
    task.on_enter(START);

    task.on_vote(1, swarm::Vote::ACK);
    task.on_vote(2, swarm::Vote::NACK);

    EXPECT_TRUE(task.is_finished());
    EXPECT_EQ(task.result(), swarm::ConsensusResult::ABORTED);
}

TEST(ConsensusTask, BeklenmeyenGondericininOyuYokSayilir)
{
    swarm::ConsensusTask task{7, {1, 2}};
    task.on_enter(START);

    task.on_vote(99, swarm::Vote::NACK);  // listede olmayan düğüm

    EXPECT_FALSE(task.is_finished());
    EXPECT_EQ(task.result(), swarm::ConsensusResult::PENDING);
}

TEST(ConsensusTask, SonucBelliOlduktanSonraGelenOylarKarariDegistirmez)
{
    swarm::ConsensusTask task{7, {1, 2}};
    task.on_enter(START);

    task.on_vote(1, swarm::Vote::NACK);
    ASSERT_EQ(task.result(), swarm::ConsensusResult::ABORTED);

    task.on_vote(2, swarm::Vote::ACK);

    EXPECT_EQ(task.result(), swarm::ConsensusResult::ABORTED);
}

TEST(ConsensusTask, OyVerecekKimseYoksaAninaCommitted)
{
    // Tek başına kalmış bir düğüm oylamada sonsuza kadar kilitlenmemeli.
    swarm::ConsensusTask task{7, {}};

    task.on_enter(START);

    EXPECT_TRUE(task.is_finished());
    EXPECT_EQ(task.result(), swarm::ConsensusResult::COMMITTED);
}

// ---------------------------------------------------------------------------
//  Faz 2.3 — ConsensusTask zaman aşımı ve görev iptali
// ---------------------------------------------------------------------------

TEST(ConsensusTaskTimeout, VarsayilanSureBesSaniye)
{
    // Bölüm 3.6'da kararlaştırılan süre. Sabit yanlışlıkla değiştirilirse
    // bu test uyarır.
    EXPECT_EQ(swarm::ConsensusTask::DEFAULT_TIMEOUT, 5000ms);
}

TEST(ConsensusTaskTimeout, SureDolmadanOylamaAcikKalir)
{
    swarm::ConsensusTask task{1, {1, 2, 3}, 5000ms};
    task.on_enter(START);

    task.run(START + 4999ms);

    EXPECT_FALSE(task.is_finished());
    EXPECT_EQ(task.result(), swarm::ConsensusResult::PENDING);
}

TEST(ConsensusTaskTimeout, BesSaniyeDolunlaGorevIptalEdilir)
{
    swarm::ConsensusTask task{1, {1, 2, 3}, 5000ms};
    task.on_enter(START);

    // Sadece iki drone cevap verdi; üçüncüsü hiç ses çıkarmadı (PENDING).
    task.on_vote(1, swarm::Vote::ACK);
    task.on_vote(2, swarm::Vote::ACK);
    ASSERT_FALSE(task.is_finished());

    task.run(START + 5000ms);

    EXPECT_TRUE(task.is_finished());
    EXPECT_EQ(task.result(), swarm::ConsensusResult::ABORTED);
    EXPECT_TRUE(task.cancelled_by_timeout());

    // Cevap vermeyen düğüm PENDING'de kalmış olmalı — "cevap yok" ile
    // "açık NACK" birbirine karıştırılmıyor.
    EXPECT_EQ(task.vote_status(3), swarm::Vote::PENDING);
}

TEST(ConsensusTaskTimeout, IptalDurumundaTumGorevIptalTalimatiVerilir)
{
    // Bölüm 2: timeout sonrası yalnızca bu task bitmez, TÜM görev iptal
    // edilir ve sürü IdleTask'a döner. Kuyruğu boşaltmak Task Engine'in
    // işidir; ConsensusTask bunu mission_should_abort() ile bildirir.
    swarm::ConsensusTask task{1, {1, 2}, 5000ms};
    task.on_enter(START);

    EXPECT_FALSE(task.mission_should_abort());

    task.run(START + 5s);

    EXPECT_TRUE(task.mission_should_abort());
}

TEST(ConsensusTaskTimeout, SureDolmadanHerkesAckVerirseCommitEdilir)
{
    swarm::ConsensusTask task{1, {1, 2}, 5000ms};
    task.on_enter(START);

    task.on_vote(1, swarm::Vote::ACK);
    task.on_vote(2, swarm::Vote::ACK);

    // Süre dolsa bile sonuç değişmez: karar zaten verildi.
    task.run(START + 10s);

    EXPECT_EQ(task.result(), swarm::ConsensusResult::COMMITTED);
    EXPECT_FALSE(task.mission_should_abort());
    EXPECT_FALSE(task.cancelled_by_timeout());
}

TEST(ConsensusTaskTimeout, NackIleIptalTimeoutIleIptalDenSayilmaz)
{
    // İkisi de ABORTED ile biter ama sebepleri farklıdır; log ve teşhis
    // için ayırt edilebilmeli.
    swarm::ConsensusTask task{1, {1, 2}, 5000ms};
    task.on_enter(START);

    task.on_vote(1, swarm::Vote::NACK);

    EXPECT_EQ(task.result(), swarm::ConsensusResult::ABORTED);
    EXPECT_TRUE(task.mission_should_abort());
    EXPECT_FALSE(task.cancelled_by_timeout());
}

TEST(ConsensusTaskTimeout, SureDolduktanSonraGelenAckKarariDegistirmez)
{
    swarm::ConsensusTask task{1, {1}, 5000ms};
    task.on_enter(START);

    task.run(START + 5s);
    ASSERT_EQ(task.result(), swarm::ConsensusResult::ABORTED);

    // Geç kalan oy kabul edilmemeli: görev çoktan iptal edildi.
    task.on_vote(1, swarm::Vote::ACK);

    EXPECT_EQ(task.result(), swarm::ConsensusResult::ABORTED);
}

// ---------------------------------------------------------------------------
//  on_enter'dan ÖNCE gelen oylar
//
//  HATA GEÇMİŞİ: Task Engine oyları on_enter()'dan önce işliyordu ve
//  on_enter() sonucu PENDING'e sıfırlıyordu. Sonuç: aynı turda gelen tüm
//  ACK'lerle COMMITTED'e ulaşan bir oylamanın kararı siliniyor, oylama
//  PENDING'de kalıp 5 saniye sonra hatalı biçimde zaman aşımına düşüyordu.
//  Yani drone'lar oy vermiş olmasına rağmen görev iptal ediliyordu.
//
//  Engine tarafındaki sıra düzeltildi; buradaki testler ConsensusTask'ın
//  kendi tarafında da dayanıklı olduğunu garanti ediyor.
// ---------------------------------------------------------------------------

TEST(ConsensusTaskErkenOy, OnEnterOncesiGelenAckSilinmez)
{
    swarm::ConsensusTask task{1, {1, 2}, 5000ms};

    // Oylar görev başlatılmadan önce geliyor.
    task.on_vote(1, swarm::Vote::ACK);
    task.on_vote(2, swarm::Vote::ACK);

    task.on_enter(START);

    EXPECT_EQ(task.result(), swarm::ConsensusResult::COMMITTED);
    EXPECT_TRUE(task.is_finished());
}

TEST(ConsensusTaskErkenOy, OnEnterOncesiGelenNackSilinmez)
{
    swarm::ConsensusTask task{1, {1, 2}, 5000ms};

    task.on_vote(1, swarm::Vote::NACK);

    task.on_enter(START);

    EXPECT_EQ(task.result(), swarm::ConsensusResult::ABORTED);
    EXPECT_TRUE(task.mission_should_abort());
    EXPECT_FALSE(task.cancelled_by_timeout());
}

TEST(ConsensusTaskErkenOy, EksikOyVarsaOnEnterSonrasiPendingKalir)
{
    // Erken oy koruması, kararı ACELEYE getirmemeli: yalnızca bir oy
    // gelmişse oylama açık kalmalı.
    swarm::ConsensusTask task{1, {1, 2}, 5000ms};

    task.on_vote(1, swarm::Vote::ACK);
    task.on_enter(START);

    EXPECT_EQ(task.result(), swarm::ConsensusResult::PENDING);
    EXPECT_FALSE(task.is_finished());

    // Kalan oy gelince COMMITTED olmalı ve zaman aşımı sayacı on_enter'dan
    // itibaren işlemeli.
    task.on_vote(2, swarm::Vote::ACK);
    EXPECT_EQ(task.result(), swarm::ConsensusResult::COMMITTED);
}
