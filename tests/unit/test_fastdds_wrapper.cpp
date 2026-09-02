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

// TCP dinleme portunun gerçekten açıldığını sınamak için ham soket API'si.
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

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

// ============================================================================
//  Faz 7 — TCP taşıyıcısı (Bölüm 3.4)
//
//  BU TESTLERİN SINIRI: aynı süreçte iki wrapper kurulduğunda Fast DDS'in
//  "intraprocess delivery" özelliği devreye girer ve veri taşıma katmanına
//  hiç inmeden doğrudan bellekten teslim edilir. Yani buradaki testler
//  "TCP yapılandırmasıyla sistem çalışıyor mu" sorusunu cevaplar;
//  "veri GERÇEKTEN TCP soketinden mi aktı" sorusunu cevaplayamazlar.
//
//  O soruyu, düğümleri ayrı container'larda çalıştıran entegrasyon testi
//  cevaplıyor: tests/integration/test_07_tcp_tasiyici.sh
// ============================================================================

// Testlerde her wrapper aynı makinede çalıştığı için dinleme portları
// FARKLI olmalıdır. Gerçek dağıtımda tüm düğümler 5100'ü kullanır; orada
// çakışma olmaz çünkü IP'leri farklıdır.
swarm::TcpTransportConfig make_test_tcp_config(uint16_t port)
{
    swarm::TcpTransportConfig config;
    config.enabled = true;
    config.local_ip = "127.0.0.1";
    config.listening_port = port;
    return config;
}

TEST(FastDDSTcp, TcpTasiyiciIleKurulumBasarili)
{
    swarm::FastDDSWrapper wrapper{TEST_DOMAIN + 5, make_test_tcp_config(5410)};
    EXPECT_TRUE(wrapper.init());
}

TEST(FastDDSTcp, TcpDinlemePortuAcik)
{
    // TCP taşıyıcısı gerçekten bir soket açtı mı? Porta bağlanmayı deneyerek
    // sınıyoruz: bağlanabiliyorsak dinleyen biri var demektir.
    swarm::FastDDSWrapper wrapper{TEST_DOMAIN + 5, make_test_tcp_config(5411)};
    ASSERT_TRUE(wrapper.init());

    const bool listening = wait_until_condition(
            [&]() {
                const int socket_fd = ::socket(AF_INET, SOCK_STREAM, 0);
                if (socket_fd < 0)
                {
                    return false;
                }

                sockaddr_in address{};
                address.sin_family = AF_INET;
                address.sin_port = htons(5411);
                ::inet_pton(AF_INET, "127.0.0.1", &address.sin_addr);

                const bool connected =
                        ::connect(socket_fd, reinterpret_cast<sockaddr*>(&address),
                                  sizeof(address)) == 0;
                ::close(socket_fd);
                return connected;
            },
            5s);

    EXPECT_TRUE(listening) << "TCP dinleme portu (5411) acilmadi";
}

TEST(FastDDSTcp, TcpKanalindaGorevEmriAkiyor)
{
    swarm::FastDDSWrapper publisher{TEST_DOMAIN + 6, make_test_tcp_config(5412)};
    swarm::FastDDSWrapper receiver{TEST_DOMAIN + 6, make_test_tcp_config(5413)};

    ASSERT_TRUE(publisher.init());
    ASSERT_TRUE(receiver.init());

    std::mutex lock;
    std::atomic<int> received_count{0};
    swarm::TaskAllocation last_received;
    receiver.set_task_allocation_callback([&](const swarm::TaskAllocation& incoming) {
        const std::lock_guard<std::mutex> guard(lock);
        last_received = incoming;
        ++received_count;
    });

    swarm::TaskAllocation order;
    order.task_id(4242);
    order.target_role(swarm::DroneRole::STRIKER);
    order.target_x(150.0);
    order.target_y(-60.0);

    const bool reached = wait_until_condition(
            [&]() {
                publisher.publish(order);
                return received_count.load() > 0;
            },
            10s);

    ASSERT_TRUE(reached) << "TCP yapilandirmasiyla gorev emri akmadi";

    const std::lock_guard<std::mutex> guard(lock);
    EXPECT_EQ(last_received.task_id(), 4242u);
    EXPECT_EQ(last_received.target_role(), swarm::DroneRole::STRIKER);
}

TEST(FastDDSTcp, TcpKanalindaConsensusOyuAkiyor)
{
    // task_alloc ve consensus AYNI TCP locator'ı ilan eder (aynı IP, aynı
    // port). İki endpoint'in tek bir adresi paylaşması RTPS'te normaldir:
    // gelen mesajın hangi endpoint'e ait olduğu, mesajın içindeki entityId
    // ile ayrılır. Bu test o paylaşımın sorunsuz çalıştığını doğruluyor.
    swarm::FastDDSWrapper publisher{TEST_DOMAIN + 7, make_test_tcp_config(5414)};
    swarm::FastDDSWrapper receiver{TEST_DOMAIN + 7, make_test_tcp_config(5415)};

    ASSERT_TRUE(publisher.init());
    ASSERT_TRUE(receiver.init());

    std::mutex lock;
    std::atomic<int> received_count{0};
    swarm::Consensus last_received;
    receiver.set_consensus_callback([&](const swarm::Consensus& incoming) {
        const std::lock_guard<std::mutex> guard(lock);
        last_received = incoming;
        ++received_count;
    });

    swarm::Consensus vote;
    vote.transaction_id(99);
    vote.sender_id(2);
    vote.vote(swarm::Vote::ACK);
    vote.seq_num(1);

    const bool reached = wait_until_condition(
            [&]() {
                publisher.publish(vote);
                return received_count.load() > 0;
            },
            10s);

    ASSERT_TRUE(reached) << "TCP yapilandirmasiyla consensus oyu akmadi";

    const std::lock_guard<std::mutex> guard(lock);
    EXPECT_EQ(last_received.transaction_id(), 99u);
    EXPECT_EQ(last_received.vote(), swarm::Vote::ACK);
}

// TRANSIENT_LOCAL + TCP'nin geç katılana ulaşması BU DOSYADA test EDİLEMEZ.
//
// Ölçüldü: aynı süreçte iki wrapper kurulduğunda canlı yayın intraprocess
// yoluyla teslim ediliyor (bu yüzden yukarıdaki testler geçiyor), ama geç
// katılan bir aboneye geçmiş örneklerin yeniden gönderilmesi çalışmıyor —
// iki participant aynı süreçte olduğu için aralarında gerçek bir TCP kanalı
// kurulmuyor ve geçmişi taşıyacak yol oluşmuyor.
//
// Ayrı süreçlerde ise doğru çalıştığı doğrulandı: geç katılan abone tam olarak
// history depth kadar (10) örneği alıyor. Gerçek doğrulama container'lar
// arasında yapılıyor: tests/integration/test_07_tcp_tasiyici.sh

TEST(FastDDSTcp, TcpKapaliykenSistemUdpIleCalismayaDevamEder)
{
    // NODE_IP verilmediğinde (tcp.enabled == false) sistem eski davranışına
    // döner: her topic UDP'den gider. Geri çekilme yolunun sağlam olduğunu
    // doğruluyoruz.
    swarm::FastDDSWrapper publisher{TEST_DOMAIN + 9};
    swarm::FastDDSWrapper receiver{TEST_DOMAIN + 9};

    ASSERT_TRUE(publisher.init());
    ASSERT_TRUE(receiver.init());

    std::atomic<int> received_count{0};
    receiver.set_task_allocation_callback(
            [&](const swarm::TaskAllocation&) { ++received_count; });

    swarm::TaskAllocation order;
    order.task_id(11);
    order.target_role(swarm::DroneRole::SCOUT);

    const bool reached = wait_until_condition(
            [&]() {
                publisher.publish(order);
                return received_count.load() > 0;
            },
            10s);

    EXPECT_TRUE(reached);
}

// ============================================================================
//  Faz 7 — Taşıyıcı ayrımının KESİN kanıtı: ilan edilen locator'lar
//
//  Bayt/segment sayaçlarıyla "hangi topic hangi taşıyıcıdan aktı" ölçmek
//  container ortamında güvenilir değil: TCP segment sayacı saf ACK'leri de
//  sayar ve keşif trafiği de TCP kullanır. Onun yerine ayrımı KAYNAĞINDA
//  doğruluyoruz: her endpoint keşif (SEDP) sırasında "bana şu adresten ulaş"
//  diye locator ilan eder; taşıyıcı seçimi tam olarak burada belirlenir.
//
//  Bu test, wrapper'ın kurduğu DDS varlıklarını DIŞARIDAN gözleyen ayrı bir
//  participant kurar ve her topic'in reader'ının ilan ettiği locator tiplerini
//  okur. Üretim kodunda hiçbir şey açığa çıkarmaya gerek yok — keşif zaten
//  ağa açık bir mekanizma.
// ============================================================================

#include <fastdds/dds/domain/DomainParticipant.hpp>
#include <fastdds/dds/domain/DomainParticipantFactory.hpp>
#include <fastdds/dds/domain/DomainParticipantListener.hpp>
#include <fastdds/rtps/transport/TCPv4TransportDescriptor.hpp>

#include <map>
#include <set>
#include <string>

namespace {

using namespace eprosima::fastdds::dds;

// topic adı -> o topic'in reader'ının ilan ettiği locator tipleri
using LocatorKindsByTopic = std::map<std::string, std::set<int32_t>>;

class LocatorObserver : public DomainParticipantListener
{
public:
    void on_data_reader_discovery(
            DomainParticipant* /*participant*/,
            eprosima::fastdds::rtps::ReaderDiscoveryStatus reason,
            const SubscriptionBuiltinTopicData& info,
            bool& /*should_be_ignored*/) override
    {
        if (reason != eprosima::fastdds::rtps::ReaderDiscoveryStatus::DISCOVERED_READER)
        {
            return;
        }

        const std::lock_guard<std::mutex> guard(lock_);
        auto& kinds = discovered_[std::string(info.topic_name)];
        for (const auto& locator : info.remote_locators.unicast)
        {
            kinds.insert(locator.kind);
        }
    }

    LocatorKindsByTopic snapshot()
    {
        const std::lock_guard<std::mutex> guard(lock_);
        return discovered_;
    }

private:
    std::mutex lock_;
    LocatorKindsByTopic discovered_;
};

}  // namespace

TEST(FastDDSTcp, GuvenilirTopicYalnizcaTcpLocatorIlanEder)
{
    constexpr uint32_t OBSERVE_DOMAIN = TEST_DOMAIN + 10;

    // Gözlemci önce kurulur ki wrapper'ın duyurularını baştan yakalasın.
    LocatorObserver observer;
    DomainParticipantQos observer_qos = PARTICIPANT_QOS_DEFAULT;
    observer_qos.name("locator_observer");

    // Gözlemcinin de TCP taşıyıcısı olmalı. Fast DDS, yerel participant'ın
    // ULAŞAMAYACAĞI locator'ları duyurudan eler; TCP'siz bir gözlemci
    // güvenilir kanalların locator listesini BOŞ görür. (Bu eleme, TCP
    // taşıyıcısı eksik bir düğümün neden sessizce UDP'ye düştüğünü de
    // açıklıyor — bkz. README V24.2.)
    auto observer_tcp =
            std::make_shared<eprosima::fastdds::rtps::TCPv4TransportDescriptor>();
    observer_tcp->add_listener_port(5421);
    observer_tcp->set_WAN_address("127.0.0.1");
    observer_qos.transport().user_transports.push_back(observer_tcp);

    DomainParticipant* observer_participant =
            DomainParticipantFactory::get_instance()->create_participant(
                    static_cast<DomainId_t>(OBSERVE_DOMAIN), observer_qos, &observer);
    ASSERT_NE(observer_participant, nullptr);

    {
        swarm::FastDDSWrapper wrapper{OBSERVE_DOMAIN, make_test_tcp_config(5420)};
        ASSERT_TRUE(wrapper.init());

        // Dört topic'in de duyurusu gelene kadar bekle.
        const bool all_discovered = wait_until_condition(
                [&]() {
                    const LocatorKindsByTopic seen = observer.snapshot();
                    if (seen.size() < 4)
                    {
                        return false;
                    }
                    for (const auto& entry : seen)
                    {
                        if (entry.second.empty())
                        {
                            return false;
                        }
                    }
                    return true;
                },
                15s);
        ASSERT_TRUE(all_discovered) << "Topic duyurulari 15 saniyede gelmedi";

        const LocatorKindsByTopic discovered = observer.snapshot();

        // --- Güvenilir kanallar: YALNIZCA TCP ---------------------------------
        for (const std::string& topic : {"swarm/task_alloc", "swarm/consensus"})
        {
            const auto entry = discovered.find(topic);
            ASSERT_NE(entry, discovered.end()) << topic << " kesfedilmedi";

            const std::set<int32_t>& kinds = entry->second;
            EXPECT_EQ(kinds.count(LOCATOR_KIND_TCPv4), 1u)
                    << topic << " TCP locator ilan etmiyor";
            EXPECT_EQ(kinds.count(LOCATOR_KIND_UDPv4), 0u)
                    << topic << " UDP locator da ilan ediyor - tasiyici ayrimi bozuk";
            EXPECT_EQ(kinds.count(LOCATOR_KIND_SHM), 0u)
                    << topic << " paylasimli bellek locator'i ilan ediyor";
            EXPECT_EQ(kinds.size(), 1u)
                    << topic << " birden fazla tasiyici tipi ilan ediyor";
        }

        // --- Yüksek frekanslı kanallar: UDP var, kısıtlanmamış ---------------
        for (const std::string& topic : {"swarm/heartbeat", "swarm/telemetry"})
        {
            const auto entry = discovered.find(topic);
            ASSERT_NE(entry, discovered.end()) << topic << " kesfedilmedi";

            EXPECT_EQ(entry->second.count(LOCATOR_KIND_UDPv4), 1u)
                    << topic << " UDP locator ilan etmiyor - UDP'de kalmali";
        }
    }

    observer_participant->delete_contained_entities();
    DomainParticipantFactory::get_instance()->delete_participant(observer_participant);
}
