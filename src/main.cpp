// ============================================================================
//  main.cpp — sürü düğümünün giriş noktası
//
//  TEK BİR ÇALIŞTIRILABİLİR, DÖRT FARKLI DÜĞÜM. Aynı binary hem GCS hem de
//  üç drone olarak çalışır; farkı ORTAM DEĞİŞKENLERİ belirler (Bölüm 4):
//
//      NODE_TYPE      DRONE | GCS            (varsayılan: DRONE)
//      DRONE_ID       0-255                  (varsayılan: 0, GCS = 0)
//      ROLE           SCOUT | STRIKER        (varsayılan: SCOUT)
//      ROS_DOMAIN_ID  DDS domain numarası    (varsayılan: 42)
//      INITIAL_BATTERY  0-100                (varsayılan: 100)
//
//  Bu sayede tek bir Docker image üretilip dört container olarak
//  çalıştırılabiliyor.
// ============================================================================

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>

#include "swarm/config_from_env.hpp"
#include "swarm/enum_names.hpp"
#include "swarm/fastdds_wrapper.hpp"
#include "swarm/gcs_controller.hpp"
#include "swarm/log.hpp"
#include "swarm/swarm_manager.hpp"

namespace {

using namespace std::chrono_literals;

// --- Kapatma işareti --------------------------------------------------------
//
// Ctrl-C veya `docker stop` (SIGTERM) geldiğinde düğümün düzgün kapanması
// gerekir: thread'ler join edilmeli, DDS varlıkları silinmeli.
//
// `volatile sig_atomic_t` yerine std::atomic<bool> kullanıyoruz; sinyal
// işleyicisinden yazılıp ana döngüden okunması güvenli.
std::atomic<bool> shutdown_requested{false};

void signal_handler(int)
{
    // Sinyal işleyicisi içinde yapılabilecekler ÇOK kısıtlıdır: bellek
    // ayırmak, lock almak, std::cout kullanmak yasaktır. Bu yüzden burada
    // yalnızca bir bayrak set ediyoruz; asıl kapanış ana döngüde oluyor.
    shutdown_requested = true;
}

// --- DDS ile SwarmManager'ı birbirine bağlama -------------------------------
//
// SwarmManager DDS'i tanımıyor, FastDDSWrapper da uygulama mantığını
// tanımıyor. İkisini birbirine bağlayan tek yer burası.
void connect_dds(swarm::FastDDSWrapper& dds, swarm::SwarmManager& manager)
{
    // Giden yön: SwarmManager yayınlamak istediğinde DDS'e verilir.
    manager.set_heartbeat_publisher(
            [&dds](const swarm::Heartbeat& message) { dds.publish(message); });
    manager.set_telemetry_publisher(
            [&dds](const swarm::Telemetry& message) { dds.publish(message); });
    manager.set_consensus_publisher(
            [&dds](const swarm::Consensus& message) { dds.publish(message); });
    manager.set_task_allocation_publisher(
            [&dds](const swarm::TaskAllocation& order) { dds.publish(order); });

    // Gelen yön: DDS'ten mesaj geldiğinde SwarmManager'a aktarılır.
    //
    // DİKKAT: bu callback'ler DDS'in KENDİ thread'inden gelir. Bu yüzden
    // içeride uzun iş yapılmıyor — veri ya mutex korumalı peer table'a
    // yazılıyor ya da komut queue'suna bırakılıp Thread 3'e devrediliyor.
    dds.set_heartbeat_callback([&manager](const swarm::Heartbeat& message) {
        manager.on_heartbeat_received(message, std::chrono::steady_clock::now());
    });

    dds.set_telemetry_callback([&manager](const swarm::Telemetry& message) {
        manager.on_telemetry_received(message, std::chrono::steady_clock::now());
    });

    dds.set_task_allocation_callback([&manager](const swarm::TaskAllocation& order) {
        manager.add_command(swarm::Command::task_order(order));
    });

    dds.set_consensus_callback([&manager](const swarm::Consensus& message) {
        manager.add_command(swarm::Command::consensus_vote(message));
    });
}

// --- GCS görev akışı --------------------------------------------------------
//
// Bölüm 3.1: GCS görev emri verir ve consensus'u başlatır. Burada iki
// görevlik sabit bir senaryo işletiliyor: önce Gözcü'ye arama, sonra
// Müdahale drone'larına hedefe gidiş. Böylece heterojen rol ayrımı gerçek
// bir akışta gözlemlenebiliyor.
struct GcsMission
{
    swarm::DroneRole target_role;
    double target_x;
    double target_y;
};

void gcs_loop(swarm::SwarmManager& manager)
{
    swarm::GcsController gcs{manager};

    const GcsMission tasks[] = {
        {swarm::DroneRole::SCOUT, 80.0, 40.0},
        {swarm::DroneRole::STRIKER, 150.0, -60.0},
    };
    constexpr std::size_t TASK_COUNT = sizeof(tasks) / sizeof(tasks[0]);

    std::size_t next_task = 0;
    bool proposal_pending = true;
    auto next_proposal_time = std::chrono::steady_clock::time_point{};

    // Keşif için kısa bir pay: drone'ların ayağa kalkıp duyulması lazım.
    // Bölüm 2'deki non-blocking strateji gereği hepsini beklemiyoruz.
    const auto discovery_deadline = std::chrono::steady_clock::now() + 8s;

    while (!shutdown_requested)
    {
        const auto current_time = std::chrono::steady_clock::now();

        if (proposal_pending && next_task < TASK_COUNT &&
            current_time >= discovery_deadline && current_time >= next_proposal_time)
        {
            const GcsMission& task = tasks[next_task];

            swarm::log("gcs", std::string("gorev teklif ediliyor rol=") +
                              swarm::drone_role_name(task.target_role) +
                              " x=" + std::to_string(task.target_x) +
                              " y=" + std::to_string(task.target_y) +
                              " (online drone=" +
                              std::to_string(manager.online_drone_ids().size()) + ")");

            gcs.propose_task(task.target_role, task.target_x, task.target_y, current_time);
            proposal_pending = false;
        }

        gcs.step(current_time);

        if (gcs.state() == swarm::GcsController::State::TASK_PUBLISHED)
        {
            swarm::log("gcs", "gorev emri yayinlandi task_id=" +
                              std::to_string(gcs.active_transaction_id()));
            ++next_task;
            proposal_pending = true;
            next_proposal_time = current_time + 5s;
            gcs.consume_result();
        }
        else if (gcs.state() == swarm::GcsController::State::CANCELLED)
        {
            swarm::log("gcs", "gorev IPTAL edildi task_id=" +
                              std::to_string(gcs.active_transaction_id()));
            ++next_task;
            proposal_pending = true;
            next_proposal_time = current_time + 5s;
            gcs.consume_result();
        }

        std::this_thread::sleep_for(200ms);
    }
}

// Drone tarafı: SwarmManager'ın thread'leri işi yapıyor; ana thread yalnızca
// kapatma işaretini bekliyor.
void drone_loop()
{
    while (!shutdown_requested)
    {
        std::this_thread::sleep_for(200ms);
    }
}

}  // namespace

int main()
{
    // SIGINT (Ctrl-C) ve SIGTERM (docker stop) yakalanıyor.
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    const swarm::ConfigResult config_result = swarm::read_config_from_env();
    if (!config_result.success)
    {
        std::cerr << "[config] HATA: " << config_result.error << std::endl;
        return 1;
    }
    const swarm::SwarmConfig config = config_result.config;

    swarm::log("node", std::string("baslatiliyor")
                       + " node_type=" + swarm::node_type_name(config.node_type)
                       + " drone_id=" + std::to_string(config.drone_id)
                       + " role=" + (config.node_type == swarm::NodeType::DRONE
                                             ? swarm::drone_role_name(config.role)
                                             : "-")
                       + " domain=" + std::to_string(config.domain_id));

    swarm::SwarmManager& manager = swarm::SwarmManager::get_instance();
    manager.init(config);
    manager.drone_state().battery = config_result.starting_battery;

    // FastDDSWrapper yığında (stack) duruyor: main'den çıkarken yıkıcısı
    // otomatik çalışıp tüm DDS varlıklarını temizler (RAII).
    swarm::FastDDSWrapper dds{config.domain_id};
    if (!dds.init())
    {
        swarm::log("node", "HATA: DDS baslatilamadi");
        return 1;
    }

    connect_dds(dds, manager);

    manager.run();
    swarm::log("node", "hazir - 3 thread calisiyor");

    if (config.node_type == swarm::NodeType::GCS)
    {
        gcs_loop(manager);
    }
    else
    {
        drone_loop();
    }

    swarm::log("node", "kapatiliyor");
    manager.stop();
    swarm::log("node", "kapandi");

    return 0;
}
