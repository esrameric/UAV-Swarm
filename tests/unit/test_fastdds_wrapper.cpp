// ============================================================================
//  Faz 4.1/4.2 — FastDDSWrapper testi
//
//  Bu test GERÇEK DDS kullanır: aynı domain'de iki wrapper kurulur, biri
//  yayınlar, diğeri alır. Yani discovery, QoS eşleşmesi ve serileştirme
//  zinciri uçtan uca sınanır.
//
//  Diğer birim testlerin aksine burada BEKLEME vardır: DDS discovery ağ
//  üzerinden gerçekleştiği için anlık değildir. Bekleme sabit süreli değil,
//  "sonuç gelene kadar, en fazla şu kadar" biçiminde yazıldı; böylece hızlı
//  makinede test hızlı biter, yavaş makinede de kırılmaz.
// ============================================================================

#include <gtest/gtest.h>

#include "swarm/fastdds_wrapper.hpp"

#include <atomic>
#include <chrono>
#include <mutex>
#include <thread>

namespace {

using namespace std::chrono_literals;

// Başka DDS/ROS 2 uygulamalarıyla çakışmasın diye testlere özel bir domain.
constexpr uint32_t TEST_DOMAIN = 77;

// Koşul sağlanana kadar bekler; en fazla `azami_sure` bekler.
// Dönüş: koşul sağlandıysa true.
template <typename Kosul>
bool kosul_saglanana_kadar_bekle(Kosul kosul, std::chrono::milliseconds azami_sure)
{
    const auto bitis = std::chrono::steady_clock::now() + azami_sure;
    while (std::chrono::steady_clock::now() < bitis)
    {
        if (kosul())
        {
            return true;
        }
        std::this_thread::sleep_for(20ms);
    }
    return kosul();
}

swarm::Heartbeat heartbeat_olustur(uint8_t drone_id)
{
    swarm::Heartbeat kalp_atisi;
    kalp_atisi.drone_id(drone_id);
    kalp_atisi.node_type(swarm::NodeType::DRONE);
    kalp_atisi.role(swarm::DroneRole::SCOUT);
    kalp_atisi.current_task(swarm::TaskType::DISCOVERY);
    return kalp_atisi;
}

}  // namespace

TEST(FastDDSWrapper, KurulumBasarili)
{
    swarm::FastDDSWrapper wrapper{TEST_DOMAIN};

    EXPECT_TRUE(wrapper.init());
}

TEST(FastDDSWrapper, BestEffortHeartbeatAkiyor)
{
    swarm::FastDDSWrapper yayinci{TEST_DOMAIN};
    swarm::FastDDSWrapper alici{TEST_DOMAIN};

    ASSERT_TRUE(yayinci.init());
    ASSERT_TRUE(alici.init());

    // Geri çağırma DDS'in KENDİ thread'inden gelir; paylaşılan veriye
    // dokunduğumuz için mutex ile koruyoruz.
    std::mutex kilit;
    std::atomic<int> alinan_sayisi{0};
    swarm::Heartbeat son_alinan;

    alici.set_heartbeat_callback([&](const swarm::Heartbeat& mesaj) {
        const std::lock_guard<std::mutex> koruma(kilit);
        son_alinan = mesaj;
        ++alinan_sayisi;
    });

    // Best-Effort olduğu için discovery tamamlanmadan yollanan paketler
    // kaybolur; bu yüzden gelene kadar tekrar tekrar yayınlıyoruz.
    const bool ulasti = kosul_saglanana_kadar_bekle(
            [&]() {
                yayinci.publish(heartbeat_olustur(2));
                return alinan_sayisi.load() > 0;
            },
            10s);

    ASSERT_TRUE(ulasti) << "Heartbeat 10 saniyede karsi tarafa ulasmadi";

    const std::lock_guard<std::mutex> koruma(kilit);
    EXPECT_EQ(son_alinan.drone_id(), 2u);
    EXPECT_EQ(son_alinan.node_type(), swarm::NodeType::DRONE);
    EXPECT_EQ(son_alinan.current_task(), swarm::TaskType::DISCOVERY);
}

TEST(FastDDSWrapper, GuvenilirGorevEmriAkiyor)
{
    swarm::FastDDSWrapper yayinci{TEST_DOMAIN};
    swarm::FastDDSWrapper alici{TEST_DOMAIN};

    ASSERT_TRUE(yayinci.init());
    ASSERT_TRUE(alici.init());

    std::mutex kilit;
    std::atomic<int> alinan_sayisi{0};
    swarm::TaskAllocation son_alinan;

    alici.set_task_allocation_callback([&](const swarm::TaskAllocation& emir) {
        const std::lock_guard<std::mutex> koruma(kilit);
        son_alinan = emir;
        ++alinan_sayisi;
    });

    swarm::TaskAllocation emir;
    emir.task_id(4242);
    emir.target_role(swarm::DroneRole::STRIKER);
    emir.target_x(120.5);
    emir.target_y(-30.25);

    const bool ulasti = kosul_saglanana_kadar_bekle(
            [&]() {
                yayinci.publish(emir);
                return alinan_sayisi.load() > 0;
            },
            10s);

    ASSERT_TRUE(ulasti) << "Gorev emri 10 saniyede karsi tarafa ulasmadi";

    const std::lock_guard<std::mutex> koruma(kilit);
    EXPECT_EQ(son_alinan.task_id(), 4242u);
    EXPECT_EQ(son_alinan.target_role(), swarm::DroneRole::STRIKER);
    EXPECT_DOUBLE_EQ(son_alinan.target_x(), 120.5);
    EXPECT_DOUBLE_EQ(son_alinan.target_y(), -30.25);
}

TEST(FastDDSWrapper, ConsensusOyuAkiyor)
{
    swarm::FastDDSWrapper yayinci{TEST_DOMAIN};
    swarm::FastDDSWrapper alici{TEST_DOMAIN};

    ASSERT_TRUE(yayinci.init());
    ASSERT_TRUE(alici.init());

    std::mutex kilit;
    std::atomic<int> alinan_sayisi{0};
    swarm::Consensus son_alinan;

    alici.set_consensus_callback([&](const swarm::Consensus& oy) {
        const std::lock_guard<std::mutex> koruma(kilit);
        son_alinan = oy;
        ++alinan_sayisi;
    });

    swarm::Consensus oy;
    oy.transaction_id(9);
    oy.sender_id(3);
    oy.vote(swarm::Vote::ACK);

    const bool ulasti = kosul_saglanana_kadar_bekle(
            [&]() {
                yayinci.publish(oy);
                return alinan_sayisi.load() > 0;
            },
            10s);

    ASSERT_TRUE(ulasti) << "Consensus oyu 10 saniyede karsi tarafa ulasmadi";

    const std::lock_guard<std::mutex> koruma(kilit);
    EXPECT_EQ(son_alinan.transaction_id(), 9u);
    EXPECT_EQ(son_alinan.sender_id(), 3u);
    EXPECT_EQ(son_alinan.vote(), swarm::Vote::ACK);
}

TEST(FastDDSWrapper, AyriDomainlerBirbiriniDuymaz)
{
    // DDS'te domain_id bir yalıtım sınırıdır: farklı domain'lerdeki
    // düğümler aynı ağda olsalar bile birbirini görmez. ROS_DOMAIN_ID
    // ayarının işlevi budur (Bölüm 9'daki çakışma notu).
    swarm::FastDDSWrapper yayinci{TEST_DOMAIN};
    swarm::FastDDSWrapper alici{TEST_DOMAIN + 1};

    ASSERT_TRUE(yayinci.init());
    ASSERT_TRUE(alici.init());

    std::atomic<int> alinan_sayisi{0};
    alici.set_heartbeat_callback([&](const swarm::Heartbeat&) { ++alinan_sayisi; });

    const bool ulasti = kosul_saglanana_kadar_bekle(
            [&]() {
                yayinci.publish(heartbeat_olustur(1));
                return alinan_sayisi.load() > 0;
            },
            2s);

    EXPECT_FALSE(ulasti);
    EXPECT_EQ(alinan_sayisi.load(), 0);
}

// ============================================================================
//  Faz 4.2 — Topic bazlı QoS ayrımının davranışa yansıması
//
//  QoS'u dışarıdan doğrudan okuyamayız; onun yerine DAVRANIŞINI sınıyoruz.
//  Ayrımı en net gösteren senaryo "geç katılan abone" (late joiner):
//    Reliable + Transient Local -> yayından SONRA katılan abone de alır.
//    Best-Effort + Volatile     -> yayından SONRA katılan abone ALMAZ.
// ============================================================================

TEST(FastDDSQos, GuvenilirKanalGecKatilanAboneyeDeUlasir)
{
    swarm::FastDDSWrapper yayinci{TEST_DOMAIN + 2};
    ASSERT_TRUE(yayinci.init());

    swarm::TaskAllocation emir;
    emir.task_id(555);
    emir.target_role(swarm::DroneRole::SCOUT);
    emir.target_x(7.0);
    emir.target_y(8.0);

    // Ortada henüz HİÇ abone yokken yayınlıyoruz.
    ASSERT_TRUE(yayinci.publish(emir));

    // Abone ancak şimdi ağa katılıyor.
    swarm::FastDDSWrapper gec_katilan{TEST_DOMAIN + 2};
    ASSERT_TRUE(gec_katilan.init());

    std::mutex kilit;
    std::atomic<int> alinan_sayisi{0};
    swarm::TaskAllocation son_alinan;
    gec_katilan.set_task_allocation_callback([&](const swarm::TaskAllocation& gelen) {
        const std::lock_guard<std::mutex> koruma(kilit);
        son_alinan = gelen;
        ++alinan_sayisi;
    });

    // TRANSIENT_LOCAL sayesinde yayıncı sakladığı örneği geç katılana yollar.
    const bool ulasti = kosul_saglanana_kadar_bekle(
            [&]() { return alinan_sayisi.load() > 0; }, 10s);

    ASSERT_TRUE(ulasti) << "Transient Local gecmis ornegi geç katilana ulasmadi";

    const std::lock_guard<std::mutex> koruma(kilit);
    EXPECT_EQ(son_alinan.task_id(), 555u);
}

TEST(FastDDSQos, BestEffortKanalGecmisiSaklamaz)
{
    swarm::FastDDSWrapper yayinci{TEST_DOMAIN + 3};
    ASSERT_TRUE(yayinci.init());

    // Abone yokken bir heartbeat yayınlıyoruz.
    ASSERT_TRUE(yayinci.publish(heartbeat_olustur(9)));

    swarm::FastDDSWrapper gec_katilan{TEST_DOMAIN + 3};
    ASSERT_TRUE(gec_katilan.init());

    std::atomic<int> alinan_sayisi{0};
    gec_katilan.set_heartbeat_callback([&](const swarm::Heartbeat&) { ++alinan_sayisi; });

    // VOLATILE olduğu için geçmiş yayın saklanmaz; yeni yayın da yapmıyoruz.
    const bool ulasti = kosul_saglanana_kadar_bekle(
            [&]() { return alinan_sayisi.load() > 0; }, 2s);

    EXPECT_FALSE(ulasti);
    EXPECT_EQ(alinan_sayisi.load(), 0);
}

TEST(FastDDSQos, BestEffortKanalYeniYayinlariNormalTasir)
{
    // Geçmişi saklamaması, kanalın çalışmadığı anlamına gelmez: abone
    // katıldıktan SONRAKİ yayınlar normal şekilde akar.
    swarm::FastDDSWrapper yayinci{TEST_DOMAIN + 4};
    swarm::FastDDSWrapper alici{TEST_DOMAIN + 4};
    ASSERT_TRUE(yayinci.init());
    ASSERT_TRUE(alici.init());

    std::atomic<int> alinan_sayisi{0};
    alici.set_telemetry_callback([&](const swarm::Telemetry&) { ++alinan_sayisi; });

    swarm::Telemetry telemetri;
    telemetri.drone_id(1);
    telemetri.seq_num(1);

    const bool ulasti = kosul_saglanana_kadar_bekle(
            [&]() {
                yayinci.publish(telemetri);
                return alinan_sayisi.load() > 0;
            },
            10s);

    EXPECT_TRUE(ulasti);
}
