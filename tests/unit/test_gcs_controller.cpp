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

const swarm::TimePoint START{};

// GCS kimliğiyle hazırlanmış, temiz bir SwarmManager.
swarm::SwarmManager& prepare_as_gcs()
{
    swarm::SwarmManager& manager = swarm::SwarmManager::get_instance();

    swarm::SwarmConfig config;
    config.drone_id = 0;
    config.node_type = swarm::NodeType::GCS;
    manager.init(config);
    manager.set_consensus_publisher(nullptr);
    manager.set_task_allocation_publisher(nullptr);

    return manager;
}

swarm::Heartbeat drone_heartbeat(uint8_t drone_id, swarm::DroneRole role)
{
    swarm::Heartbeat heartbeat;
    heartbeat.drone_id(drone_id);
    heartbeat.node_type(swarm::NodeType::DRONE);
    heartbeat.role(role);
    heartbeat.current_task(swarm::TaskType::IDLE);
    return heartbeat;
}

// Bir drone'un oyunu komut queue'suna koyar (ağdan gelmiş gibi).
void send_vote(swarm::SwarmManager& manager,
               uint32_t transaction_id,
               uint8_t drone_id,
               swarm::Vote vote)
{
    swarm::Consensus message;
    message.transaction_id(transaction_id);
    message.sender_id(drone_id);
    message.vote(vote);
    manager.add_command(swarm::Command::consensus_vote(message));
}

// Üç drone'u ONLINE yapar (1 Scout + 2 Striker — Bölüm 3.1'deki sürü).
void bring_up_swarm(swarm::SwarmManager& manager)
{
    manager.on_heartbeat_received(drone_heartbeat(1, swarm::DroneRole::SCOUT), START);
    manager.on_heartbeat_received(drone_heartbeat(2, swarm::DroneRole::STRIKER), START);
    manager.on_heartbeat_received(drone_heartbeat(3, swarm::DroneRole::STRIKER), START);
}

}  // namespace

TEST(GcsController, BaslangictaBosta)
{
    swarm::SwarmManager& manager = prepare_as_gcs();
    const swarm::GcsController gcs{manager};

    EXPECT_EQ(gcs.state(), swarm::GcsController::State::IDLE);
}

TEST(GcsController, TeklifYayinlanirVeOylamayaGecilir)
{
    swarm::SwarmManager& manager = prepare_as_gcs();
    bring_up_swarm(manager);

    // Yayınlanan consensus mesajlarını yakalıyoruz.
    std::vector<swarm::Consensus> received_messages;
    manager.set_consensus_publisher(
            [&received_messages](const swarm::Consensus& message) {
                received_messages.push_back(message);
            });

    swarm::GcsController gcs{manager};
    const uint32_t transaction_no = gcs.propose_task(
            swarm::DroneRole::STRIKER, 100.0, 50.0, START);

    EXPECT_EQ(gcs.state(), swarm::GcsController::State::VOTING);
    ASSERT_EQ(received_messages.size(), 1u);

    // TEKLİF mesajının işareti: vote alanı PENDING.
    EXPECT_EQ(received_messages[0].vote(), swarm::Vote::PENDING);
    EXPECT_EQ(received_messages[0].transaction_id(), transaction_no);
    EXPECT_EQ(received_messages[0].sender_id(), 0u);

    manager.set_consensus_publisher(nullptr);
}

TEST(GcsController, TeklifAnindaGorevEmriYayinlanmaz)
{
    // Emir ancak oybirliğinden SONRA yayınlanır.
    swarm::SwarmManager& manager = prepare_as_gcs();
    bring_up_swarm(manager);

    int published_order_count = 0;
    manager.set_task_allocation_publisher(
            [&published_order_count](const swarm::TaskAllocation&) {
                ++published_order_count;
            });

    swarm::GcsController gcs{manager};
    gcs.propose_task(swarm::DroneRole::SCOUT, 10.0, 10.0, START);

    EXPECT_EQ(published_order_count, 0);

    manager.set_task_allocation_publisher(nullptr);
}

TEST(GcsController, OybirligindeGorevEmriYayinlanir)
{
    swarm::SwarmManager& manager = prepare_as_gcs();
    bring_up_swarm(manager);

    std::vector<swarm::TaskAllocation> published_orders;
    manager.set_task_allocation_publisher(
            [&published_orders](const swarm::TaskAllocation& order) {
                published_orders.push_back(order);
            });

    swarm::GcsController gcs{manager};
    const uint32_t transaction_no = gcs.propose_task(
            swarm::DroneRole::STRIKER, 250.0, -75.0, START);

    // Oylama başlasın.
    manager.task_engine_step(START);

    // Üç drone da ACK veriyor.
    send_vote(manager, transaction_no, 1, swarm::Vote::ACK);
    send_vote(manager, transaction_no, 2, swarm::Vote::ACK);
    send_vote(manager, transaction_no, 3, swarm::Vote::ACK);
    manager.task_engine_step(START + 100ms);

    gcs.step(START + 100ms);

    EXPECT_EQ(gcs.state(), swarm::GcsController::State::TASK_PUBLISHED);
    ASSERT_EQ(published_orders.size(), 1u);
    EXPECT_EQ(published_orders[0].task_id(), transaction_no);
    EXPECT_EQ(published_orders[0].target_role(), swarm::DroneRole::STRIKER);
    EXPECT_DOUBLE_EQ(published_orders[0].target_x(), 250.0);
    EXPECT_DOUBLE_EQ(published_orders[0].target_y(), -75.0);

    manager.set_task_allocation_publisher(nullptr);
}

TEST(GcsController, TekNackGoreviIptalEderVeEmirYayinlanmaz)
{
    swarm::SwarmManager& manager = prepare_as_gcs();
    bring_up_swarm(manager);

    int published_order_count = 0;
    manager.set_task_allocation_publisher(
            [&published_order_count](const swarm::TaskAllocation&) {
                ++published_order_count;
            });

    swarm::GcsController gcs{manager};
    const uint32_t transaction_no = gcs.propose_task(
            swarm::DroneRole::SCOUT, 10.0, 10.0, START);

    manager.task_engine_step(START);

    send_vote(manager, transaction_no, 1, swarm::Vote::ACK);
    send_vote(manager, transaction_no, 2, swarm::Vote::NACK);   // biri hayır diyor
    manager.task_engine_step(START + 100ms);

    gcs.step(START + 100ms);

    EXPECT_EQ(gcs.state(), swarm::GcsController::State::CANCELLED);
    EXPECT_EQ(published_order_count, 0);

    manager.set_task_allocation_publisher(nullptr);
}

TEST(GcsController, CevapsizDroneVarsaBesSaniyeSonundaIptal)
{
    // Bölüm 3.6: 5 saniye içinde tam ACK sağlanamazsa görev iptal edilir.
    swarm::SwarmManager& manager = prepare_as_gcs();
    bring_up_swarm(manager);

    int published_order_count = 0;
    manager.set_task_allocation_publisher(
            [&published_order_count](const swarm::TaskAllocation&) {
                ++published_order_count;
            });

    swarm::GcsController gcs{manager};
    const uint32_t transaction_no = gcs.propose_task(
            swarm::DroneRole::SCOUT, 10.0, 10.0, START);

    manager.task_engine_step(START);

    // Yalnızca iki drone cevap veriyor; üçüncüsü sessiz.
    send_vote(manager, transaction_no, 1, swarm::Vote::ACK);
    send_vote(manager, transaction_no, 2, swarm::Vote::ACK);
    manager.task_engine_step(START + 100ms);
    gcs.step(START + 100ms);
    ASSERT_EQ(gcs.state(), swarm::GcsController::State::VOTING);

    // 5 saniye doluyor.
    manager.task_engine_step(START + 5s);
    gcs.step(START + 5s);

    EXPECT_EQ(gcs.state(), swarm::GcsController::State::CANCELLED);
    EXPECT_EQ(published_order_count, 0);
    EXPECT_TRUE(manager.last_consensus_result().cancelled_by_timeout);

    manager.set_task_allocation_publisher(nullptr);
}

TEST(GcsController, HicDroneYokkenTeklifAninaGecer)
{
    // Sürüde hiç drone yoksa oy bekleyecek kimse de yok: oylama anında
    // COMMITTED olur ve emir yayınlanır (V16).
    swarm::SwarmManager& manager = prepare_as_gcs();

    int published_order_count = 0;
    manager.set_task_allocation_publisher(
            [&published_order_count](const swarm::TaskAllocation&) {
                ++published_order_count;
            });

    swarm::GcsController gcs{manager};
    gcs.propose_task(swarm::DroneRole::SCOUT, 5.0, 5.0, START);

    manager.task_engine_step(START);
    gcs.step(START);

    EXPECT_EQ(gcs.state(), swarm::GcsController::State::TASK_PUBLISHED);
    EXPECT_EQ(published_order_count, 1);

    manager.set_task_allocation_publisher(nullptr);
}

// --- Drone tarafı: teklife oy verme ----------------------------------------

TEST(DroneOyVerme, TeklifAlanDroneAckYayinlar)
{
    swarm::SwarmManager& manager = swarm::SwarmManager::get_instance();
    swarm::SwarmConfig config;
    config.drone_id = 2;
    config.node_type = swarm::NodeType::DRONE;
    config.role = swarm::DroneRole::STRIKER;
    manager.init(config);

    std::vector<swarm::Consensus> received_messages;
    manager.set_consensus_publisher(
            [&received_messages](const swarm::Consensus& message) {
                received_messages.push_back(message);
            });

    // GCS'ten teklif geliyor (vote = PENDING).
    swarm::Consensus proposal;
    proposal.transaction_id(55);
    proposal.sender_id(0);
    proposal.vote(swarm::Vote::PENDING);
    manager.add_command(swarm::Command::consensus_vote(proposal));

    manager.task_engine_step(START);

    ASSERT_EQ(received_messages.size(), 1u);
    EXPECT_EQ(received_messages[0].transaction_id(), 55u);
    EXPECT_EQ(received_messages[0].sender_id(), 2u);
    EXPECT_EQ(received_messages[0].vote(), swarm::Vote::ACK);

    manager.set_consensus_publisher(nullptr);
}

TEST(DroneOyVerme, BataryasiKritikOlanDroneNackYayinlar)
{
    swarm::SwarmManager& manager = swarm::SwarmManager::get_instance();
    swarm::SwarmConfig config;
    config.drone_id = 3;
    config.node_type = swarm::NodeType::DRONE;
    config.role = swarm::DroneRole::STRIKER;
    manager.init(config);

    manager.drone_state().battery = 5;   // kritik

    std::vector<swarm::Consensus> received_messages;
    manager.set_consensus_publisher(
            [&received_messages](const swarm::Consensus& message) {
                received_messages.push_back(message);
            });

    swarm::Consensus proposal;
    proposal.transaction_id(56);
    proposal.sender_id(0);
    proposal.vote(swarm::Vote::PENDING);
    manager.add_command(swarm::Command::consensus_vote(proposal));

    manager.task_engine_step(START);

    ASSERT_EQ(received_messages.size(), 1u);
    EXPECT_EQ(received_messages[0].vote(), swarm::Vote::NACK);

    manager.set_consensus_publisher(nullptr);
}

TEST(DroneOyVerme, GcsKendiTeklifineOyVermez)
{
    // GCS bir drone değildir; kendi teklifine oy üretmemeli.
    swarm::SwarmManager& manager = prepare_as_gcs();

    std::vector<swarm::Consensus> received_messages;
    manager.set_consensus_publisher(
            [&received_messages](const swarm::Consensus& message) {
                received_messages.push_back(message);
            });

    swarm::Consensus proposal;
    proposal.transaction_id(57);
    proposal.sender_id(9);   // başka bir düğümden gelmiş gibi
    proposal.vote(swarm::Vote::PENDING);
    manager.add_command(swarm::Command::consensus_vote(proposal));

    manager.task_engine_step(START);

    EXPECT_TRUE(received_messages.empty());

    manager.set_consensus_publisher(nullptr);
}
