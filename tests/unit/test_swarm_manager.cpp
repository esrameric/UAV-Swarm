// ============================================================================
//  Faz 3.1 — SwarmManager Singleton garantisinin testi
// ============================================================================

#include <gtest/gtest.h>

#include "swarm/swarm_manager.hpp"

#include <thread>
#include <type_traits>
#include <vector>

TEST(SwarmManagerSingleton, HerCagriAyniOrnegiDondurur)
{
    // İki ayrı çağrının AYNI nesneyi verdiğini adreslerini karşılaştırarak
    // doğruluyoruz. `&` bir nesnenin bellekteki adresini verir.
    swarm::SwarmManager& first = swarm::SwarmManager::get_instance();
    swarm::SwarmManager& second_result = swarm::SwarmManager::get_instance();

    EXPECT_EQ(&first, &second_result);
}

TEST(SwarmManagerSingleton, KopyalanamazVeTasinamaz)
{
    // std::is_copy_constructible<T>::value, "T kopyalanabilir mi?" sorusunu
    // DERLEME ZAMANINDA cevaplar. Kopyalama silindiği için false olmalı.
    // Bu, singleton garantisinin kazara kırılmasına karşı koruma.
    static_assert(!std::is_copy_constructible<swarm::SwarmManager>::value,
                  "SwarmManager kopyalanabilir olmamali");
    static_assert(!std::is_copy_assignable<swarm::SwarmManager>::value,
                  "SwarmManager kopya atamasi kabul etmemeli");
    static_assert(!std::is_move_constructible<swarm::SwarmManager>::value,
                  "SwarmManager tasinabilir olmamali");

    EXPECT_FALSE(std::is_copy_constructible<swarm::SwarmManager>::value);
}

TEST(SwarmManagerSingleton, DisaridanOrnekOlusturulamaz)
{
    // Kurucu private olduğu için `swarm::SwarmManager m;` yazımı derlenmez.
    // Bunu derleme zamanında şöyle doğruluyoruz:
    static_assert(!std::is_default_constructible<swarm::SwarmManager>::value,
                  "SwarmManager disaridan kurulabilir olmamali");

    EXPECT_FALSE(std::is_default_constructible<swarm::SwarmManager>::value);
}

TEST(SwarmManagerSingleton, EszamanliErisimdeDeTekOrnek)
{
    // Meyers Singleton'ın asıl vaadi: birden fazla thread aynı anda
    // get_instance() çağırsa bile nesne yalnızca BİR KEZ kurulur ve
    // hepsi aynı adresi görür.
    //
    // std::thread: işletim sisteminden yeni bir çalışma iş parçacığı ister.
    constexpr int THREAD_COUNT = 8;
    std::vector<swarm::SwarmManager*> addresses(THREAD_COUNT, nullptr);
    std::vector<std::thread> threads;

    for (int index = 0; index < THREAD_COUNT; ++index)
    {
        // LAMBDA: yerinde tanımlanan isimsiz fonksiyon. Köşeli parantez
        // içindeki `&adresler, sira` "yakalama listesi"dir: lambda'nın
        // dışarıdaki hangi değişkenleri kullanacağını söyler. `&` referansla
        // (asıl nesne), isim tek başına ise kopyayla yakalar.
        threads.emplace_back([&addresses, index]() {
            addresses[static_cast<std::size_t>(index)] = &swarm::SwarmManager::get_instance();
        });
    }

    // join(): thread'in bitmesini bekler. Beklemezsek main thread önce
    // bitip programı sonlandırabilir.
    for (std::thread& thread : threads)
    {
        thread.join();
    }

    for (const swarm::SwarmManager* address : addresses)
    {
        EXPECT_EQ(address, &swarm::SwarmManager::get_instance());
    }
}

// ============================================================================
//  Faz 3.2 — mutex korumalı üyeler
//
//  DİKKAT: SwarmManager bir Singleton olduğu için testler AYNI nesneyi
//  paylaşır. Bu yüzden her test, işe başlamadan önce queue'ları boşaltır;
//  böylece testlerin çalışma sırası sonucu etkilemez.
// ============================================================================

#include "swarm/command.hpp"
#include "swarm/task/consensus_task.hpp"
#include "swarm/task/hover_task.hpp"
#include "swarm/task/idle_task.hpp"
#include "swarm/task/scout_search_task.hpp"
#include "swarm/task/init_task.hpp"

namespace {

using namespace std::chrono_literals;

// Testlerin sabit zaman referansı (bkz. test_tasks.cpp'deki aynı kalıp).
const swarm::TimePoint START{};

void clear_queues(swarm::SwarmManager& manager)
{
    swarm::Command discarded;
    while (manager.pop_command(discarded))
    {
    }
    manager.clear_task_queue();
}

swarm::TaskAllocation create_task_order(uint32_t task_id, swarm::DroneRole role)
{
    swarm::TaskAllocation order;
    order.task_id(task_id);
    order.target_role(role);
    order.target_x(10.0);
    order.target_y(20.0);
    return order;
}

}  // namespace

TEST(SwarmManagerKuyruk, BaslangictaBosaltilabiliyor)
{
    swarm::SwarmManager& manager = swarm::SwarmManager::get_instance();
    clear_queues(manager);

    EXPECT_EQ(manager.command_queue_size(), 0u);
    EXPECT_EQ(manager.task_queue_size(), 0u);
    EXPECT_EQ(manager.current_task(), nullptr);
}

TEST(SwarmManagerKuyruk, KomutlarFifoSirasiylaIslenir)
{
    swarm::SwarmManager& manager = swarm::SwarmManager::get_instance();
    clear_queues(manager);

    manager.add_command(swarm::Command::task_order(
            create_task_order(1, swarm::DroneRole::SCOUT)));
    manager.add_command(swarm::Command::task_order(
            create_task_order(2, swarm::DroneRole::STRIKER)));

    EXPECT_EQ(manager.command_queue_size(), 2u);

    swarm::Command first;
    ASSERT_TRUE(manager.pop_command(first));
    EXPECT_EQ(first.type, swarm::CommandType::TASK_ALLOCATION);
    EXPECT_EQ(first.task_allocation.task_id(), 1u);

    swarm::Command second_result;
    ASSERT_TRUE(manager.pop_command(second_result));
    EXPECT_EQ(second_result.task_allocation.task_id(), 2u);

    EXPECT_EQ(manager.command_queue_size(), 0u);
}

TEST(SwarmManagerKuyruk, BosKuyruktanPopFalseDoner)
{
    swarm::SwarmManager& manager = swarm::SwarmManager::get_instance();
    clear_queues(manager);

    swarm::Command output;
    EXPECT_FALSE(manager.pop_command(output));
}

TEST(SwarmManagerKuyruk, ConsensusOyuKomutOlarakTasinabilir)
{
    swarm::SwarmManager& manager = swarm::SwarmManager::get_instance();
    clear_queues(manager);

    swarm::Consensus vote;
    vote.transaction_id(42);
    vote.sender_id(3);
    vote.vote(swarm::Vote::ACK);

    manager.add_command(swarm::Command::consensus_vote(vote));

    swarm::Command received;
    ASSERT_TRUE(manager.pop_command(received));
    EXPECT_EQ(received.type, swarm::CommandType::CONSENSUS);
    EXPECT_EQ(received.consensus.transaction_id(), 42u);
    EXPECT_EQ(received.consensus.vote(), swarm::Vote::ACK);
}

TEST(SwarmManagerKuyruk, GorevKuyruguSirayiKorur)
{
    swarm::SwarmManager& manager = swarm::SwarmManager::get_instance();
    clear_queues(manager);

    swarm::DroneState state;
    manager.push_task(std::make_unique<swarm::InitTask>());
    manager.push_task(std::make_unique<swarm::IdleTask>(state));

    EXPECT_EQ(manager.task_queue_size(), 2u);

    // Aktif görev queue'nun BAŞIDIR.
    ASSERT_NE(manager.current_task(), nullptr);
    EXPECT_EQ(manager.current_task()->get_type(), swarm::TaskType::INIT);
}

TEST(SwarmManagerKuyruk, ClearTaskQueueTumGorevleriIptalEder)
{
    // Consensus ABORTED olduğunda sürünün IdleTask'a dönebilmesi için
    // queue'nun boşaltılabilmesi gerekir (Bölüm 2/3.6).
    swarm::SwarmManager& manager = swarm::SwarmManager::get_instance();
    clear_queues(manager);

    swarm::DroneState state;
    manager.push_task(std::make_unique<swarm::InitTask>());
    manager.push_task(std::make_unique<swarm::IdleTask>(state));
    ASSERT_EQ(manager.task_queue_size(), 2u);

    manager.clear_task_queue();

    EXPECT_EQ(manager.task_queue_size(), 0u);
    EXPECT_EQ(manager.current_task(), nullptr);
}

TEST(SwarmManagerKuyruk, PeerTablosuBaslangictaBos)
{
    const swarm::SwarmManager& manager = swarm::SwarmManager::get_instance();

    // Bu test henüz peer eklenmemişken çalışır; peer ekleme Faz 3.5'te
    // update_peer_list() ile gelecek.
    EXPECT_EQ(manager.online_peer_count(), 0u);
}

TEST(SwarmManagerKuyruk, EszamanliKomutEklemeVeriYarisiYaratmaz)
{
    // MUTEX'İN ASIL SINANDIĞI TEST: 4 thread aynı anda komut ekliyor.
    // Kilit olmasaydı deque'nun iç yapısı bozulur, sayı tutmaz veya
    // program çökerdi.
    swarm::SwarmManager& manager = swarm::SwarmManager::get_instance();
    clear_queues(manager);

    constexpr int THREAD_COUNT = 4;
    constexpr int COMMAND_COUNT = 250;

    std::vector<std::thread> threads;
    for (int thread_index = 0; thread_index < THREAD_COUNT; ++thread_index)
    {
        threads.emplace_back([&manager, thread_index]() {
            for (int index = 0; index < COMMAND_COUNT; ++index)
            {
                manager.add_command(swarm::Command::task_order(
                        create_task_order(
                                static_cast<uint32_t>(thread_index * 1000 + index),
                                swarm::DroneRole::SCOUT)));
            }
        });
    }

    for (std::thread& thread : threads)
    {
        thread.join();
    }

    // Hiçbir komut kaybolmamalı, fazladan da olmamalı.
    EXPECT_EQ(manager.command_queue_size(),
              static_cast<std::size_t>(THREAD_COUNT * COMMAND_COUNT));

    clear_queues(manager);
}

// ============================================================================
//  Faz 3.3 — init() ve run()
// ============================================================================

TEST(SwarmManagerYasamDongusu, InitKimligiAyarlar)
{
    swarm::SwarmManager& manager = swarm::SwarmManager::get_instance();

    swarm::SwarmConfig config;
    config.drone_id = 2;
    config.node_type = swarm::NodeType::DRONE;
    config.role = swarm::DroneRole::STRIKER;
    config.domain_id = 42;

    manager.init(config);

    EXPECT_EQ(manager.config().drone_id, 2u);
    EXPECT_EQ(manager.config().node_type, swarm::NodeType::DRONE);
    EXPECT_EQ(manager.config().role, swarm::DroneRole::STRIKER);
    EXPECT_EQ(manager.config().domain_id, 42u);
}

TEST(SwarmManagerYasamDongusu, InitKuyruklariVeDurumuSifirlar)
{
    swarm::SwarmManager& manager = swarm::SwarmManager::get_instance();

    // Önce kirletiyoruz...
    manager.add_command(swarm::Command::task_order(
            create_task_order(1, swarm::DroneRole::SCOUT)));
    manager.push_task(std::make_unique<swarm::InitTask>());
    manager.drone_state().x = 123.0;
    ASSERT_GT(manager.command_queue_size(), 0u);

    // ...init temizlemeli.
    manager.init(swarm::SwarmConfig{});

    EXPECT_EQ(manager.command_queue_size(), 0u);
    EXPECT_EQ(manager.task_queue_size(), 0u);
    EXPECT_EQ(manager.peer_count(), 0u);
    EXPECT_DOUBLE_EQ(manager.drone_state().x, 0.0);
}

TEST(SwarmManagerYasamDongusu, GcsKimligiKurulabilir)
{
    swarm::SwarmManager& manager = swarm::SwarmManager::get_instance();

    swarm::SwarmConfig config;
    config.drone_id = 0;
    config.node_type = swarm::NodeType::GCS;

    manager.init(config);

    EXPECT_EQ(manager.config().node_type, swarm::NodeType::GCS);
    EXPECT_EQ(manager.config().drone_id, 0u);
}

TEST(SwarmManagerYasamDongusu, RunThreadleriBaslatirStopBekler)
{
    swarm::SwarmManager& manager = swarm::SwarmManager::get_instance();
    manager.init(swarm::SwarmConfig{});

    EXPECT_FALSE(manager.is_running());

    manager.run();
    EXPECT_TRUE(manager.is_running());

    // stop() thread'lerin bitmesini BEKLER (join). Dönüş sonrası hiçbir
    // thread çalışmıyor olmalı.
    manager.stop();
    EXPECT_FALSE(manager.is_running());
}

TEST(SwarmManagerYasamDongusu, IkinciRunCagrisiEtkisiz)
{
    swarm::SwarmManager& manager = swarm::SwarmManager::get_instance();
    manager.init(swarm::SwarmConfig{});

    manager.run();
    manager.run();  // ikinci çağrı yeni thread AÇMAMALI
    EXPECT_TRUE(manager.is_running());

    manager.stop();
    EXPECT_FALSE(manager.is_running());
}

TEST(SwarmManagerYasamDongusu, CalismayanDugumdeStopGuvenli)
{
    swarm::SwarmManager& manager = swarm::SwarmManager::get_instance();
    manager.init(swarm::SwarmConfig{});

    // Hiç run() çağrılmadan stop() çağırmak çökmemelidir.
    manager.stop();
    manager.stop();

    EXPECT_FALSE(manager.is_running());
}

TEST(SwarmManagerYasamDongusu, BasDurBasDurDongusuCalisir)
{
    // Thread'ler düzgün join edildiği için düğüm tekrar tekrar
    // başlatılıp durdurulabilmeli.
    swarm::SwarmManager& manager = swarm::SwarmManager::get_instance();
    manager.init(swarm::SwarmConfig{});

    for (int round = 0; round < 3; ++round)
    {
        manager.run();
        ASSERT_TRUE(manager.is_running());
        manager.stop();
        ASSERT_FALSE(manager.is_running());
    }
}

// ============================================================================
//  Faz 3.4 — Task Engine (Thread 3) davranışı
//
//  Thread başlatmadan test ediyoruz: task_engine_adimi(now) tek bir turdur,
//  thread yalnızca onu döngüde çağırır. Zamanı biz verdiğimiz için
//  senaryolar deterministik.
// ============================================================================

#include "swarm/task_allocation_engine.hpp"

namespace {

swarm::SwarmManager& prepare_as_drone(uint8_t drone_id, swarm::DroneRole role)
{
    swarm::SwarmManager& manager = swarm::SwarmManager::get_instance();

    swarm::SwarmConfig config;
    config.drone_id = drone_id;
    config.node_type = swarm::NodeType::DRONE;
    config.role = role;

    manager.init(config);
    return manager;
}

}  // namespace

TEST(TaskEngine, BosKuyrukIdleTaskaDuser)
{
    swarm::SwarmManager& manager = prepare_as_drone(1, swarm::DroneRole::SCOUT);
    ASSERT_EQ(manager.task_queue_size(), 0u);

    manager.task_engine_step(START);

    ASSERT_NE(manager.current_task(), nullptr);
    EXPECT_EQ(manager.current_task()->get_type(), swarm::TaskType::IDLE);
}

TEST(TaskEngine, BitenGorevKuyruktanCikarilirVeSiradakiBaslar)
{
    swarm::SwarmManager& manager = prepare_as_drone(1, swarm::DroneRole::SCOUT);

    // InitTask ilk run()'da biter; ardindan HoverTask aktif olmali.
    manager.push_task(std::make_unique<swarm::InitTask>());
    manager.push_task(std::make_unique<swarm::HoverTask>(manager.drone_state(), 1000ms));
    ASSERT_EQ(manager.task_queue_size(), 2u);

    manager.task_engine_step(START);

    EXPECT_EQ(manager.task_queue_size(), 1u);
    ASSERT_NE(manager.current_task(), nullptr);
    EXPECT_EQ(manager.current_task()->get_type(), swarm::TaskType::HOVER);
}

TEST(TaskEngine, KuyrukTukeninceIdleTaskaDonulur)
{
    swarm::SwarmManager& manager = prepare_as_drone(1, swarm::DroneRole::SCOUT);
    manager.push_task(std::make_unique<swarm::InitTask>());

    manager.task_engine_step(START);          // InitTask biter
    manager.task_engine_step(START + 20ms);   // queue bos -> IdleTask

    ASSERT_NE(manager.current_task(), nullptr);
    EXPECT_EQ(manager.current_task()->get_type(), swarm::TaskType::IDLE);
}

// --- Gorev dagitimi (TaskAllocationEngine) ---------------------------------

TEST(TaskEngine, ScoutRoluAramaGoreviAlir)
{
    swarm::SwarmManager& manager = prepare_as_drone(1, swarm::DroneRole::SCOUT);

    manager.add_command(swarm::Command::task_order(
            create_task_order(10, swarm::DroneRole::SCOUT)));

    manager.task_engine_step(START);

    ASSERT_NE(manager.current_task(), nullptr);
    EXPECT_EQ(manager.current_task()->get_type(), swarm::TaskType::SCOUT_SEARCH);
}

TEST(TaskEngine, StrikerRoluHedefeGidisGoreviAlir)
{
    swarm::SwarmManager& manager = prepare_as_drone(2, swarm::DroneRole::STRIKER);

    manager.add_command(swarm::Command::task_order(
            create_task_order(11, swarm::DroneRole::STRIKER)));

    manager.task_engine_step(START);

    ASSERT_NE(manager.current_task(), nullptr);
    EXPECT_EQ(manager.current_task()->get_type(), swarm::TaskType::GO_TO_TARGET);
}

TEST(TaskEngine, BaskaRoleGonderilenEmirYokSayilir)
{
    // Heterojen sure: emir bir DRONE'a degil bir ROLE gonderilir.
    swarm::SwarmManager& manager = prepare_as_drone(1, swarm::DroneRole::SCOUT);

    manager.add_command(swarm::Command::task_order(
            create_task_order(12, swarm::DroneRole::STRIKER)));

    manager.task_engine_step(START);

    // Emir yok sayildi -> IdleTask'ta kaldik.
    ASSERT_NE(manager.current_task(), nullptr);
    EXPECT_EQ(manager.current_task()->get_type(), swarm::TaskType::IDLE);
}

TEST(TaskEngine, GcsUcusGoreviAlmaz)
{
    swarm::SwarmManager& manager = swarm::SwarmManager::get_instance();
    swarm::SwarmConfig config;
    config.drone_id = 0;
    config.node_type = swarm::NodeType::GCS;
    manager.init(config);

    manager.add_command(swarm::Command::task_order(
            create_task_order(13, swarm::DroneRole::SCOUT)));

    manager.task_engine_step(START);

    ASSERT_NE(manager.current_task(), nullptr);
    EXPECT_EQ(manager.current_task()->get_type(), swarm::TaskType::IDLE);
}

TEST(TaskEngine, YeniGorevEmriIdleTaskiYerindenEder)
{
    swarm::SwarmManager& manager = prepare_as_drone(1, swarm::DroneRole::SCOUT);

    manager.task_engine_step(START);
    ASSERT_EQ(manager.current_task()->get_type(), swarm::TaskType::IDLE);

    manager.add_command(swarm::Command::task_order(
            create_task_order(14, swarm::DroneRole::SCOUT)));
    manager.task_engine_step(START + 20ms);

    // Bosta beklemeye devam etmenin anlami yok: yeni goreve geciliyor.
    EXPECT_EQ(manager.current_task()->get_type(), swarm::TaskType::SCOUT_SEARCH);
    EXPECT_EQ(manager.task_queue_size(), 1u);
}

// --- Consensus oylarinin yonlendirilmesi -----------------------------------

TEST(TaskEngine, ConsensusOyuAktifOylamayaIletilir)
{
    swarm::SwarmManager& manager = prepare_as_drone(1, swarm::DroneRole::SCOUT);

    auto voting = std::make_unique<swarm::ConsensusTask>(
            77, std::vector<uint8_t>{2, 3}, 5000ms);
    swarm::ConsensusTask* voting_ptr = voting.get();
    manager.push_task(std::move(voting));

    swarm::Consensus vote;
    vote.transaction_id(77);
    vote.sender_id(2);
    vote.vote(swarm::Vote::ACK);
    manager.add_command(swarm::Command::consensus_vote(vote));

    manager.task_engine_step(START);

    EXPECT_EQ(voting_ptr->vote_status(2), swarm::Vote::ACK);
}

TEST(TaskEngine, YanlisTransactionIdliOyYokSayilir)
{
    swarm::SwarmManager& manager = prepare_as_drone(1, swarm::DroneRole::SCOUT);

    auto voting = std::make_unique<swarm::ConsensusTask>(
            77, std::vector<uint8_t>{2}, 5000ms);
    swarm::ConsensusTask* voting_ptr = voting.get();
    manager.push_task(std::move(voting));

    swarm::Consensus vote;
    vote.transaction_id(999);   // baska bir oylama turuna ait
    vote.sender_id(2);
    vote.vote(swarm::Vote::ACK);
    manager.add_command(swarm::Command::consensus_vote(vote));

    manager.task_engine_step(START);

    EXPECT_EQ(voting_ptr->vote_status(2), swarm::Vote::PENDING);
}

TEST(TaskEngine, ConsensusIptalindeTumGorevKuyruguBosaltilir)
{
    // Bolum 2/3.6: oylama basarisizsa yalnizca o task bitmez, TUM gorev
    // iptal edilir ve suru IdleTask'a doner.
    swarm::SwarmManager& manager = prepare_as_drone(1, swarm::DroneRole::SCOUT);

    manager.push_task(std::make_unique<swarm::ConsensusTask>(
            88, std::vector<uint8_t>{2, 3}, 5000ms));
    // Oylama gecerse yapilacak gorevler de queue'da bekliyor:
    manager.push_task(std::make_unique<swarm::ScoutSearchTask>(
            manager.drone_state(), 100.0, 100.0));
    manager.push_task(std::make_unique<swarm::HoverTask>(
            manager.drone_state(), 1000ms));
    ASSERT_EQ(manager.task_queue_size(), 3u);

    // Oylama baslar ama kimse cevap vermez; 5 saniye sonra timeout.
    manager.task_engine_step(START);
    ASSERT_EQ(manager.task_queue_size(), 3u);

    manager.task_engine_step(START + 5s);

    // Bekleyen TUM gorevler iptal edildi.
    EXPECT_EQ(manager.task_queue_size(), 0u);

    // Bir sonraki turda IdleTask'a dusuluyor.
    manager.task_engine_step(START + 5s + 20ms);
    ASSERT_NE(manager.current_task(), nullptr);
    EXPECT_EQ(manager.current_task()->get_type(), swarm::TaskType::IDLE);
}

TEST(TaskEngine, BasariliConsensusSonrasiSiradakiGoreveGecilir)
{
    swarm::SwarmManager& manager = prepare_as_drone(1, swarm::DroneRole::SCOUT);

    manager.push_task(std::make_unique<swarm::ConsensusTask>(
            99, std::vector<uint8_t>{2}, 5000ms));
    manager.push_task(std::make_unique<swarm::ScoutSearchTask>(
            manager.drone_state(), 10.0, 0.0));

    manager.task_engine_step(START);

    swarm::Consensus vote;
    vote.transaction_id(99);
    vote.sender_id(2);
    vote.vote(swarm::Vote::ACK);
    manager.add_command(swarm::Command::consensus_vote(vote));

    manager.task_engine_step(START + 20ms);

    // Oylama COMMITTED oldu, gorev iptal edilmedi, siradakine gecildi.
    ASSERT_NE(manager.current_task(), nullptr);
    EXPECT_EQ(manager.current_task()->get_type(), swarm::TaskType::SCOUT_SEARCH);
}

// --- TaskAllocationEngine dogrudan -----------------------------------------

TEST(TaskAllocationEngine, RolEslesmesiniDogruKarar)
{
    swarm::SwarmConfig scout;
    scout.node_type = swarm::NodeType::DRONE;
    scout.role = swarm::DroneRole::SCOUT;

    swarm::SwarmConfig gcs;
    gcs.node_type = swarm::NodeType::GCS;

    const swarm::TaskAllocation scout_order = create_task_order(1, swarm::DroneRole::SCOUT);
    const swarm::TaskAllocation striker_order = create_task_order(2, swarm::DroneRole::STRIKER);

    EXPECT_TRUE(swarm::TaskAllocationEngine::concerns_this_node(scout_order, scout));
    EXPECT_FALSE(swarm::TaskAllocationEngine::concerns_this_node(striker_order, scout));
    EXPECT_FALSE(swarm::TaskAllocationEngine::concerns_this_node(scout_order, gcs));
}

// ============================================================================
//  Faz 3.5 — check_emergency / update_peer_list / send_self_status
// ============================================================================

namespace {

// GCS kimliğiyle hazırlanmış temiz bir SwarmManager (bu dosyaya özel).
swarm::SwarmManager& prepare_as_gcs_local()
{
    swarm::SwarmManager& manager = swarm::SwarmManager::get_instance();
    swarm::SwarmConfig config;
    config.drone_id = 0;
    config.node_type = swarm::NodeType::GCS;
    manager.init(config);
    return manager;
}

swarm::Heartbeat create_peer_heartbeat(uint8_t drone_id, swarm::DroneRole role)
{
    swarm::Heartbeat heartbeat;
    heartbeat.drone_id(drone_id);
    heartbeat.node_type(swarm::NodeType::DRONE);
    heartbeat.role(role);
    heartbeat.current_task(swarm::TaskType::IDLE);
    return heartbeat;
}

}  // namespace

// --- update_peer_list ------------------------------------------------------

TEST(UpdatePeerList, GelenHeartbeatPeeriTabloyaEkler)
{
    swarm::SwarmManager& manager = prepare_as_drone(1, swarm::DroneRole::SCOUT);
    ASSERT_EQ(manager.peer_count(), 0u);

    manager.on_heartbeat_received(create_peer_heartbeat(2, swarm::DroneRole::STRIKER),
                                   START);

    EXPECT_EQ(manager.peer_count(), 1u);
    EXPECT_EQ(manager.online_peer_count(), 1u);
}

TEST(UpdatePeerList, KendiYayinimizTabloyaEklenmez)
{
    // Multicast'te kendi heartbeat'imizi geri duyabiliriz; kendimizi peer
    // olarak saymamalıyız.
    swarm::SwarmManager& manager = prepare_as_drone(1, swarm::DroneRole::SCOUT);

    manager.on_heartbeat_received(create_peer_heartbeat(1, swarm::DroneRole::SCOUT),
                                   START);

    EXPECT_EQ(manager.peer_count(), 0u);
}

TEST(UpdatePeerList, SusanPeerZamanAsimiylaOfflineOlur)
{
    swarm::SwarmManager& manager = prepare_as_drone(1, swarm::DroneRole::SCOUT);

    manager.on_heartbeat_received(create_peer_heartbeat(2, swarm::DroneRole::STRIKER),
                                   START);
    ASSERT_EQ(manager.online_peer_count(), 1u);

    // Varsayılan zaman aşımı 3 saniye (PeerManager).
    manager.update_peer_list(START + 2s);
    EXPECT_EQ(manager.online_peer_count(), 1u);

    manager.update_peer_list(START + 4s);
    EXPECT_EQ(manager.online_peer_count(), 0u);
    // Kayıt silinmez: "tanıyorduk ama şimdi kayıp" bilgisi korunur.
    EXPECT_EQ(manager.peer_count(), 1u);
}

TEST(UpdatePeerList, GeriDonenPeerTekrarOnlineOlur)
{
    swarm::SwarmManager& manager = prepare_as_drone(1, swarm::DroneRole::SCOUT);

    manager.on_heartbeat_received(create_peer_heartbeat(2, swarm::DroneRole::STRIKER),
                                   START);
    manager.update_peer_list(START + 4s);
    ASSERT_EQ(manager.online_peer_count(), 0u);

    manager.on_heartbeat_received(create_peer_heartbeat(2, swarm::DroneRole::STRIKER),
                                   START + 5s);

    EXPECT_EQ(manager.online_peer_count(), 1u);
}

TEST(UpdatePeerList, KendiTelemetrimizReddedilir)
{
    swarm::SwarmManager& manager = prepare_as_drone(1, swarm::DroneRole::SCOUT);

    swarm::Telemetry own;
    own.drone_id(1);
    own.seq_num(5);

    EXPECT_FALSE(manager.on_telemetry_received(own, START));
}

// --- send_self_status ------------------------------------------------------

TEST(SendSelfStatus, HeartbeatDogruAlanlarlaUretilir)
{
    swarm::SwarmManager& manager = prepare_as_drone(3, swarm::DroneRole::STRIKER);

    const swarm::Heartbeat produced = manager.build_heartbeat();

    EXPECT_EQ(produced.drone_id(), 3u);
    EXPECT_EQ(produced.node_type(), swarm::NodeType::DRONE);
    EXPECT_EQ(produced.role(), swarm::DroneRole::STRIKER);
    // Kuyruk boşken INIT bildirilir.
    EXPECT_EQ(produced.current_task(), swarm::TaskType::INIT);
}

TEST(SendSelfStatus, HeartbeatAktifGoreviBildirir)
{
    swarm::SwarmManager& manager = prepare_as_drone(1, swarm::DroneRole::SCOUT);

    manager.task_engine_step(START);  // IdleTask aktif olur

    EXPECT_EQ(manager.build_heartbeat().current_task(), swarm::TaskType::IDLE);
}

TEST(SendSelfStatus, TelemetriSeqNumHerYayindaArtar)
{
    swarm::SwarmManager& manager = prepare_as_drone(1, swarm::DroneRole::SCOUT);

    const uint32_t first = manager.build_telemetry(START).seq_num();
    const uint32_t second_result = manager.build_telemetry(START).seq_num();
    const uint32_t third = manager.build_telemetry(START).seq_num();

    EXPECT_EQ(second_result, first + 1);
    EXPECT_EQ(third, first + 2);
}

TEST(SendSelfStatus, TelemetriKendiUcusDurumunuTasir)
{
    swarm::SwarmManager& manager = prepare_as_drone(2, swarm::DroneRole::STRIKER);
    manager.drone_state().x = 12.5;
    manager.drone_state().z = 40.0;
    manager.drone_state().battery = 73;

    const swarm::Telemetry telemetry = manager.build_telemetry(START);

    EXPECT_EQ(telemetry.drone_id(), 2u);
    EXPECT_DOUBLE_EQ(telemetry.x(), 12.5);
    EXPECT_DOUBLE_EQ(telemetry.z(), 40.0);
    EXPECT_EQ(telemetry.battery(), 73u);
}

TEST(SendSelfStatus, YayinlayiciBagliysaCagirilir)
{
    swarm::SwarmManager& manager = prepare_as_drone(1, swarm::DroneRole::SCOUT);

    // Yayın kanalına bir lambda bağlıyoruz; DDS'in yerini test içinde bu
    // tutuyor. SwarmManager'ın DDS'i hiç tanımadan yayın yapabilmesinin
    // sebebi bu std::function katmanı.
    int heartbeat_count = 0;
    int telemetry_count = 0;
    manager.set_heartbeat_publisher(
            [&heartbeat_count](const swarm::Heartbeat&) { ++heartbeat_count; });
    manager.set_telemetry_publisher(
            [&telemetry_count](const swarm::Telemetry&) { ++telemetry_count; });

    manager.send_self_status(START);
    manager.send_self_status(START + 100ms);

    EXPECT_EQ(heartbeat_count, 2);
    EXPECT_EQ(telemetry_count, 2);

    // Sonraki testleri etkilememesi için kanalları boşaltıyoruz.
    manager.set_heartbeat_publisher(nullptr);
    manager.set_telemetry_publisher(nullptr);
}

// --- check_emergency -------------------------------------------------------

TEST(CheckEmergency, SaglikliDurumdaAcilDurumYok)
{
    swarm::SwarmManager& manager = prepare_as_drone(1, swarm::DroneRole::SCOUT);

    EXPECT_FALSE(manager.check_emergency(START));
}

TEST(CheckEmergency, KritikBataryaAcilDurumdur)
{
    swarm::SwarmManager& manager = prepare_as_drone(1, swarm::DroneRole::SCOUT);

    manager.drone_state().battery = swarm::SwarmManager::CRITICAL_BATTERY_PERCENTAGE;
    EXPECT_FALSE(manager.check_emergency(START));

    manager.drone_state().battery =
            static_cast<uint8_t>(swarm::SwarmManager::CRITICAL_BATTERY_PERCENTAGE - 1);
    EXPECT_TRUE(manager.check_emergency(START));
}

TEST(CheckEmergency, HicDuyulmayanDroneAcilDurumSayilmaz)
{
    // Bölüm 2'deki non-blocking keşif: bir drone hiç ayağa kalkmayabilir.
    // Tabloya hiç girmediği için acil durum tetiklenmemeli.
    swarm::SwarmManager& manager = prepare_as_drone(1, swarm::DroneRole::SCOUT);

    manager.update_peer_list(START + 10s);

    EXPECT_FALSE(manager.check_emergency(START + 10s));
}

TEST(CheckEmergency, GelipKaybolanDroneAcilDurumdur)
{
    swarm::SwarmManager& manager = prepare_as_drone(1, swarm::DroneRole::SCOUT);

    manager.on_heartbeat_received(create_peer_heartbeat(2, swarm::DroneRole::STRIKER),
                                   START);
    ASSERT_FALSE(manager.check_emergency(START));

    // Peer susuyor -> zaman aşımı -> OFFLINE -> acil durum.
    manager.update_peer_list(START + 4s);

    EXPECT_TRUE(manager.check_emergency(START + 4s));
}

TEST(CheckEmergency, AcilDurumdaFailSafeVeLandingKuyrugaGirer)
{
    // Faz 6.4'ün birim test karşılığı: bir düğüm kaybolunca FailSafeTask
    // tetiklenmeli.
    swarm::SwarmManager& manager = prepare_as_drone(1, swarm::DroneRole::SCOUT);

    manager.on_heartbeat_received(create_peer_heartbeat(2, swarm::DroneRole::STRIKER),
                                   START);
    manager.push_task(std::make_unique<swarm::ScoutSearchTask>(
            manager.drone_state(), 500.0, 500.0));

    // Peer kayboluyor.
    manager.update_peer_list(START + 4s);
    manager.task_engine_step(START + 4s);

    // Devam eden görev iptal edilip güvenli diziye geçilmeli.
    ASSERT_NE(manager.current_task(), nullptr);
    EXPECT_EQ(manager.current_task()->get_type(), swarm::TaskType::FAIL_SAFE);

    // FailSafe bitince iniş gelir.
    manager.task_engine_step(START + 6s);
    ASSERT_NE(manager.current_task(), nullptr);
    EXPECT_EQ(manager.current_task()->get_type(), swarm::TaskType::LANDING);
}

TEST(CheckEmergency, AcilDurumGorevleriHerTurdaTekrarEklenmez)
{
    swarm::SwarmManager& manager = prepare_as_drone(1, swarm::DroneRole::SCOUT);

    manager.on_heartbeat_received(create_peer_heartbeat(2, swarm::DroneRole::STRIKER),
                                   START);
    manager.update_peer_list(START + 4s);

    manager.task_engine_step(START + 4s);
    const std::size_t initial_size = manager.task_queue_size();

    manager.task_engine_step(START + 4s + 20ms);
    manager.task_engine_step(START + 4s + 40ms);

    // Kuyruk büyümemeli; acil durum görevleri yalnızca ilk girişte eklenir.
    EXPECT_LE(manager.task_queue_size(), initial_size);
}

// ---------------------------------------------------------------------------
//  Görev tipi kaydı — Faz 6.4 entegrasyon testinin ortaya çıkardığı hata
// ---------------------------------------------------------------------------

TEST(TaskEngine, BitenGorevinArdindanSiradakininTipiKaydedilir)
{
    // HATA GEÇMİŞİ: on_enter/bayrak/tip-kaydı üçlüsü iki ayrı yerde elle
    // yazılmıştı ve "görev bitti, sıradakine geç" dalında tip kaydı
    // unutulmuştu. Sonuç: FailSafeTask bitip LandingTask başladığında
    // current_task_type() FAIL_SAFE'te kalıyor, heartbeat yanlış görev
    // bildiriyor ve geçiş log'a hiç yazılmıyordu.
    swarm::SwarmManager& manager = prepare_as_drone(1, swarm::DroneRole::SCOUT);

    manager.push_task(std::make_unique<swarm::InitTask>());
    manager.push_task(std::make_unique<swarm::HoverTask>(manager.drone_state(), 1000ms));

    // InitTask tek turda hem başlar hem biter; aynı adımda HoverTask
    // devralır. Asıl sınanan şey: devralan görevin tipi KAYDEDİLİYOR mu?
    manager.task_engine_step(START);

    EXPECT_EQ(manager.current_task_type(), swarm::TaskType::HOVER);
    ASSERT_NE(manager.current_task(), nullptr);
    EXPECT_EQ(manager.current_task()->get_type(), swarm::TaskType::HOVER);

    // HoverTask süresi dolmadan tip değişmemeli.
    manager.task_engine_step(START + 20ms);
    EXPECT_EQ(manager.current_task_type(), swarm::TaskType::HOVER);
}

TEST(TaskEngine, AcilDurumdaFailSafeSonrasiLandingTipiKaydedilir)
{
    // Faz 6.4'ün birim test karşılığı: FailSafe -> Landing geçişi hem
    // queue'da hem de kaydedilen tipte görünmeli.
    swarm::SwarmManager& manager = prepare_as_drone(1, swarm::DroneRole::SCOUT);

    manager.on_heartbeat_received(create_peer_heartbeat(2, swarm::DroneRole::STRIKER),
                                   START);
    manager.update_peer_list(START + 4s);

    manager.task_engine_step(START + 4s);
    ASSERT_EQ(manager.current_task_type(), swarm::TaskType::FAIL_SAFE);

    // FailSafe değerlendirme süresi (1 sn) dolunca iniş başlamalı.
    manager.task_engine_step(START + 6s);
    EXPECT_EQ(manager.current_task_type(), swarm::TaskType::LANDING);
}

TEST(TaskEngine, AyniTurdaGelenTumOylarConsensusuCommitEder)
{
    // HATA GEÇMİŞİ (entegrasyon testinin ortaya çıkardığı yarış):
    // Task Engine komutları aktif görevi başlatmadan ÖNCE işliyordu.
    // ConsensusTask::on_enter() sonucu PENDING'e sıfırladığı için, aynı
    // turda gelen tüm ACK'lerle verilmiş COMMITTED kararı siliniyor ve
    // oylama 5 saniye sonra hatalı biçimde zaman aşımına düşüyordu.
    swarm::SwarmManager& manager = prepare_as_gcs_local();

    manager.request_consensus(500, {1, 2, 3});

    // Oylar, görev daha queue'ya girmeden komut queue'suna düşüyor —
    // gerçek sistemde ağ, Task Engine'in 20 ms'lik turundan hızlı davranıyor.
    for (uint8_t drone_id : {1, 2, 3})
    {
        swarm::Consensus vote;
        vote.transaction_id(500);
        vote.sender_id(drone_id);
        vote.vote(swarm::Vote::ACK);
        manager.add_command(swarm::Command::consensus_vote(vote));
    }

    manager.task_engine_step(START);

    const swarm::SwarmManager::ConsensusOutcome result = manager.last_consensus_result();
    ASSERT_TRUE(result.valid);
    EXPECT_EQ(result.transaction_id, 500u);
    EXPECT_EQ(result.result, swarm::ConsensusResult::COMMITTED);
    EXPECT_FALSE(result.cancelled_by_timeout);
}

TEST(TaskEngine, AyniTurdaGelenNackConsensusuIptalEder)
{
    swarm::SwarmManager& manager = prepare_as_gcs_local();

    manager.request_consensus(501, {1, 2});

    swarm::Consensus vote;
    vote.transaction_id(501);
    vote.sender_id(1);
    vote.vote(swarm::Vote::NACK);
    manager.add_command(swarm::Command::consensus_vote(vote));

    manager.task_engine_step(START);

    const swarm::SwarmManager::ConsensusOutcome result = manager.last_consensus_result();
    ASSERT_TRUE(result.valid);
    EXPECT_EQ(result.result, swarm::ConsensusResult::ABORTED);
    EXPECT_FALSE(result.cancelled_by_timeout);
}
