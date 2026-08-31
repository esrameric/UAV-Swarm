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

// Koşul sağlanana kadar bekler; en fazla `max_duration` bekler.
// Dönüş: koşul sağlandıysa true.
template <typename Condition>
bool wait_until_condition(Condition condition, std::chrono::milliseconds max_duration)
{
    const auto end_time = std::chrono::steady_clock::now() + max_duration;
    while (std::chrono::steady_clock::now() < end_time)
    {
        if (condition())
        {
            return true;
        }
        std::this_thread::sleep_for(20ms);
    }
    return condition();
}

swarm::Heartbeat create_heartbeat(uint8_t drone_id)
{
    swarm::Heartbeat heartbeat;
    heartbeat.drone_id(drone_id);
    heartbeat.node_type(swarm::NodeType::DRONE);
    heartbeat.role(swarm::DroneRole::SCOUT);
    heartbeat.current_task(swarm::TaskType::DISCOVERY);
    return heartbeat;
}

}  // namespace

TEST(FastDDSWrapper, KurulumBasarili)
{
    swarm::FastDDSWrapper wrapper{TEST_DOMAIN};

    EXPECT_TRUE(wrapper.init());
}

TEST(FastDDSWrapper, BestEffortHeartbeatAkiyor)
{
    swarm::FastDDSWrapper publisher{TEST_DOMAIN};
    swarm::FastDDSWrapper receiver{TEST_DOMAIN};

    ASSERT_TRUE(publisher.init());
    ASSERT_TRUE(receiver.init());

    // Geri çağırma DDS'in KENDİ thread'inden gelir; paylaşılan veriye
    // dokunduğumuz için mutex ile koruyoruz.
    std::mutex lock;
    std::atomic<int> received_count{0};
    swarm::Heartbeat last_received;

    receiver.set_heartbeat_callback([&](const swarm::Heartbeat& message) {
        const std::lock_guard<std::mutex> guard(lock);
        last_received = message;
        ++received_count;
    });

    // Best-Effort olduğu için discovery tamamlanmadan yollanan paketler
    // kaybolur; bu yüzden gelene kadar tekrar tekrar yayınlıyoruz.
    const bool reached = wait_until_condition(
            [&]() {
                publisher.publish(create_heartbeat(2));
                return received_count.load() > 0;
            },
            10s);

    ASSERT_TRUE(reached) << "Heartbeat 10 saniyede karsi tarafa ulasmadi";

    const std::lock_guard<std::mutex> guard(lock);
    EXPECT_EQ(last_received.drone_id(), 2u);
    EXPECT_EQ(last_received.node_type(), swarm::NodeType::DRONE);
    EXPECT_EQ(last_received.current_task(), swarm::TaskType::DISCOVERY);
}

TEST(FastDDSWrapper, GuvenilirGorevEmriAkiyor)
{
    swarm::FastDDSWrapper publisher{TEST_DOMAIN};
    swarm::FastDDSWrapper receiver{TEST_DOMAIN};

    ASSERT_TRUE(publisher.init());
    ASSERT_TRUE(receiver.init());

    std::mutex lock;
    std::atomic<int> received_count{0};
    swarm::TaskAllocation last_received;

    receiver.set_task_allocation_callback([&](const swarm::TaskAllocation& order) {
        const std::lock_guard<std::mutex> guard(lock);
        last_received = order;
        ++received_count;
    });

    swarm::TaskAllocation order;
    order.task_id(4242);
    order.target_role(swarm::DroneRole::STRIKER);
    order.target_x(120.5);
    order.target_y(-30.25);

    const bool reached = wait_until_condition(
            [&]() {
                publisher.publish(order);
                return received_count.load() > 0;
            },
            10s);

    ASSERT_TRUE(reached) << "Gorev emri 10 saniyede karsi tarafa ulasmadi";

    const std::lock_guard<std::mutex> guard(lock);
    EXPECT_EQ(last_received.task_id(), 4242u);
    EXPECT_EQ(last_received.target_role(), swarm::DroneRole::STRIKER);
    EXPECT_DOUBLE_EQ(last_received.target_x(), 120.5);
    EXPECT_DOUBLE_EQ(last_received.target_y(), -30.25);
}

TEST(FastDDSWrapper, ConsensusOyuAkiyor)
{
    swarm::FastDDSWrapper publisher{TEST_DOMAIN};
    swarm::FastDDSWrapper receiver{TEST_DOMAIN};

    ASSERT_TRUE(publisher.init());
    ASSERT_TRUE(receiver.init());

    std::mutex lock;
    std::atomic<int> received_count{0};
    swarm::Consensus last_received;

    receiver.set_consensus_callback([&](const swarm::Consensus& vote) {
        const std::lock_guard<std::mutex> guard(lock);
        last_received = vote;
        ++received_count;
    });

    swarm::Consensus vote;
    vote.transaction_id(9);
    vote.sender_id(3);
    vote.vote(swarm::Vote::ACK);

    const bool reached = wait_until_condition(
            [&]() {
                publisher.publish(vote);
                return received_count.load() > 0;
            },
            10s);

    ASSERT_TRUE(reached) << "Consensus oyu 10 saniyede karsi tarafa ulasmadi";

    const std::lock_guard<std::mutex> guard(lock);
    EXPECT_EQ(last_received.transaction_id(), 9u);
    EXPECT_EQ(last_received.sender_id(), 3u);
    EXPECT_EQ(last_received.vote(), swarm::Vote::ACK);
}

TEST(FastDDSWrapper, AyriDomainlerBirbiriniDuymaz)
{
    // DDS'te domain_id bir yalıtım sınırıdır: farklı domain'lerdeki
    // düğümler aynı ağda olsalar bile birbirini görmez. ROS_DOMAIN_ID
    // ayarının işlevi budur (Bölüm 9'daki çakışma notu).
    swarm::FastDDSWrapper publisher{TEST_DOMAIN};
    swarm::FastDDSWrapper receiver{TEST_DOMAIN + 1};

    ASSERT_TRUE(publisher.init());
    ASSERT_TRUE(receiver.init());

    std::atomic<int> received_count{0};
    receiver.set_heartbeat_callback([&](const swarm::Heartbeat&) { ++received_count; });

    const bool reached = wait_until_condition(
            [&]() {
                publisher.publish(create_heartbeat(1));
                return received_count.load() > 0;
            },
            2s);

    EXPECT_FALSE(reached);
    EXPECT_EQ(received_count.load(), 0);
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
    swarm::FastDDSWrapper publisher{TEST_DOMAIN + 2};
    ASSERT_TRUE(publisher.init());

    swarm::TaskAllocation order;
    order.task_id(555);
    order.target_role(swarm::DroneRole::SCOUT);
    order.target_x(7.0);
    order.target_y(8.0);

    // Ortada henüz HİÇ abone yokken yayınlıyoruz.
    ASSERT_TRUE(publisher.publish(order));

    // Abone ancak şimdi ağa katılıyor.
    swarm::FastDDSWrapper late_joiner{TEST_DOMAIN + 2};
    ASSERT_TRUE(late_joiner.init());

    std::mutex lock;
    std::atomic<int> received_count{0};
    swarm::TaskAllocation last_received;
    late_joiner.set_task_allocation_callback([&](const swarm::TaskAllocation& incoming) {
        const std::lock_guard<std::mutex> guard(lock);
        last_received = incoming;
        ++received_count;
    });

    // TRANSIENT_LOCAL sayesinde yayıncı sakladığı örneği geç katılana yollar.
    const bool reached = wait_until_condition(
            [&]() { return received_count.load() > 0; }, 10s);

    ASSERT_TRUE(reached) << "Transient Local gecmis ornegi geç katilana ulasmadi";

    const std::lock_guard<std::mutex> guard(lock);
    EXPECT_EQ(last_received.task_id(), 555u);
}

TEST(FastDDSQos, BestEffortKanalGecmisiSaklamaz)
{
    swarm::FastDDSWrapper publisher{TEST_DOMAIN + 3};
    ASSERT_TRUE(publisher.init());

    // Abone yokken bir heartbeat yayınlıyoruz.
    ASSERT_TRUE(publisher.publish(create_heartbeat(9)));

    swarm::FastDDSWrapper late_joiner{TEST_DOMAIN + 3};
    ASSERT_TRUE(late_joiner.init());

    std::atomic<int> received_count{0};
    late_joiner.set_heartbeat_callback([&](const swarm::Heartbeat&) { ++received_count; });

    // VOLATILE olduğu için geçmiş yayın saklanmaz; yeni yayın da yapmıyoruz.
    const bool reached = wait_until_condition(
            [&]() { return received_count.load() > 0; }, 2s);

    EXPECT_FALSE(reached);
    EXPECT_EQ(received_count.load(), 0);
}

TEST(FastDDSQos, BestEffortKanalYeniYayinlariNormalTasir)
{
    // Geçmişi saklamaması, kanalın çalışmadığı anlamına gelmez: abone
    // katıldıktan SONRAKİ yayınlar normal şekilde akar.
    swarm::FastDDSWrapper publisher{TEST_DOMAIN + 4};
    swarm::FastDDSWrapper receiver{TEST_DOMAIN + 4};
    ASSERT_TRUE(publisher.init());
    ASSERT_TRUE(receiver.init());

    std::atomic<int> received_count{0};
    receiver.set_telemetry_callback([&](const swarm::Telemetry&) { ++received_count; });

    swarm::Telemetry telemetry;
    telemetry.drone_id(1);
    telemetry.seq_num(1);

    const bool reached = wait_until_condition(
            [&]() {
                publisher.publish(telemetry);
                return received_count.load() > 0;
            },
            10s);

    EXPECT_TRUE(reached);
}
