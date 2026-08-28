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
std::atomic<bool> kapatma_istendi{false};

void sinyal_isleyici(int)
{
    // Sinyal işleyicisi içinde yapılabilecekler ÇOK kısıtlıdır: bellek
    // ayırmak, kilit almak, std::cout kullanmak yasaktır. Bu yüzden burada
    // yalnızca bir bayrak set ediyoruz; asıl kapanış ana döngüde oluyor.
    kapatma_istendi = true;
}

// --- DDS ile SwarmManager'ı birbirine bağlama -------------------------------
//
// SwarmManager DDS'i tanımıyor, FastDDSWrapper da uygulama mantığını
// tanımıyor. İkisini birbirine bağlayan tek yer burası.
void dds_ile_bagla(swarm::FastDDSWrapper& dds, swarm::SwarmManager& yonetici)
{
    // Giden yön: SwarmManager yayınlamak istediğinde DDS'e verilir.
    yonetici.set_heartbeat_publisher(
            [&dds](const swarm::Heartbeat& mesaj) { dds.publish(mesaj); });
    yonetici.set_telemetry_publisher(
            [&dds](const swarm::Telemetry& mesaj) { dds.publish(mesaj); });
    yonetici.set_consensus_publisher(
            [&dds](const swarm::Consensus& mesaj) { dds.publish(mesaj); });
    yonetici.set_task_allocation_publisher(
            [&dds](const swarm::TaskAllocation& emir) { dds.publish(emir); });

    // Gelen yön: DDS'ten mesaj geldiğinde SwarmManager'a aktarılır.
    //
    // DİKKAT: bu geri çağırmalar DDS'in KENDİ thread'inden gelir. Bu yüzden
    // içeride uzun iş yapılmıyor — veri ya mutex korumalı peer table'a
    // yazılıyor ya da komut kuyruğuna bırakılıp Thread 3'e devrediliyor.
    dds.set_heartbeat_callback([&yonetici](const swarm::Heartbeat& mesaj) {
        yonetici.on_heartbeat_received(mesaj, std::chrono::steady_clock::now());
    });

    dds.set_telemetry_callback([&yonetici](const swarm::Telemetry& mesaj) {
        yonetici.on_telemetry_received(mesaj, std::chrono::steady_clock::now());
    });

    dds.set_task_allocation_callback([&yonetici](const swarm::TaskAllocation& emir) {
        yonetici.add_command(swarm::Command::gorev_emri(emir));
    });

    dds.set_consensus_callback([&yonetici](const swarm::Consensus& mesaj) {
        yonetici.add_command(swarm::Command::consensus_oyu(mesaj));
    });
}

// --- GCS görev akışı --------------------------------------------------------
//
// Bölüm 3.1: GCS görev emri verir ve consensus'u başlatır. Burada iki
// görevlik sabit bir senaryo işletiliyor: önce Gözcü'ye arama, sonra
// Müdahale drone'larına hedefe gidiş. Böylece heterojen rol ayrımı gerçek
// bir akışta gözlemlenebiliyor.
struct GcsGorevi
{
    swarm::DroneRole hedef_rol;
    double hedef_x;
    double hedef_y;
};

void gcs_dongusu(swarm::SwarmManager& yonetici)
{
    swarm::GcsController gcs{yonetici};

    const GcsGorevi gorevler[] = {
        {swarm::DroneRole::SCOUT, 80.0, 40.0},
        {swarm::DroneRole::STRIKER, 150.0, -60.0},
    };
    constexpr std::size_t GOREV_SAYISI = sizeof(gorevler) / sizeof(gorevler[0]);

    std::size_t sonraki_gorev = 0;
    bool teklif_bekleniyor = true;
    auto sonraki_teklif_zamani = std::chrono::steady_clock::time_point{};

    // Keşif için kısa bir pay: drone'ların ayağa kalkıp duyulması lazım.
    // Bölüm 2'deki non-blocking strateji gereği hepsini beklemiyoruz.
    const auto kesif_bitisi = std::chrono::steady_clock::now() + 8s;

    while (!kapatma_istendi)
    {
        const auto simdi = std::chrono::steady_clock::now();

        if (teklif_bekleniyor && sonraki_gorev < GOREV_SAYISI &&
            simdi >= kesif_bitisi && simdi >= sonraki_teklif_zamani)
        {
            const GcsGorevi& gorev = gorevler[sonraki_gorev];

            swarm::log("gcs", std::string("gorev teklif ediliyor rol=") +
                              swarm::drone_role_adi(gorev.hedef_rol) +
                              " x=" + std::to_string(gorev.hedef_x) +
                              " y=" + std::to_string(gorev.hedef_y) +
                              " (online drone=" +
                              std::to_string(yonetici.online_drone_ids().size()) + ")");

            gcs.gorev_teklif_et(gorev.hedef_rol, gorev.hedef_x, gorev.hedef_y, simdi);
            teklif_bekleniyor = false;
        }

        gcs.adim(simdi);

        if (gcs.durum() == swarm::GcsController::Durum::GOREV_YAYINLANDI)
        {
            swarm::log("gcs", "gorev emri yayinlandi task_id=" +
                              std::to_string(gcs.aktif_transaction_id()));
            ++sonraki_gorev;
            teklif_bekleniyor = true;
            sonraki_teklif_zamani = simdi + 3s;
            gcs.sonucu_tuket();
        }
        else if (gcs.durum() == swarm::GcsController::Durum::IPTAL)
        {
            swarm::log("gcs", "gorev IPTAL edildi task_id=" +
                              std::to_string(gcs.aktif_transaction_id()));
            ++sonraki_gorev;
            teklif_bekleniyor = true;
            sonraki_teklif_zamani = simdi + 3s;
            gcs.sonucu_tuket();
        }

        std::this_thread::sleep_for(200ms);
    }
}

// Drone tarafı: SwarmManager'ın thread'leri işi yapıyor; ana thread yalnızca
// kapatma işaretini bekliyor.
void drone_dongusu()
{
    while (!kapatma_istendi)
    {
        std::this_thread::sleep_for(200ms);
    }
}

}  // namespace

int main()
{
    // SIGINT (Ctrl-C) ve SIGTERM (docker stop) yakalanıyor.
    std::signal(SIGINT, sinyal_isleyici);
    std::signal(SIGTERM, sinyal_isleyici);

    const swarm::ConfigSonucu config_sonucu = swarm::config_ortamdan_oku();
    if (!config_sonucu.basarili)
    {
        std::cerr << "[config] HATA: " << config_sonucu.hata << std::endl;
        return 1;
    }
    const swarm::SwarmConfig config = config_sonucu.config;

    swarm::log("node", std::string("baslatiliyor")
                       + " node_type=" + swarm::node_type_adi(config.node_type)
                       + " drone_id=" + std::to_string(config.drone_id)
                       + " role=" + (config.node_type == swarm::NodeType::DRONE
                                             ? swarm::drone_role_adi(config.role)
                                             : "-")
                       + " domain=" + std::to_string(config.domain_id));

    swarm::SwarmManager& yonetici = swarm::SwarmManager::get_instance();
    yonetici.init(config);
    yonetici.drone_state().battery = config_sonucu.baslangic_bataryasi;

    // FastDDSWrapper yığında (stack) duruyor: main'den çıkarken yıkıcısı
    // otomatik çalışıp tüm DDS varlıklarını temizler (RAII).
    swarm::FastDDSWrapper dds{config.domain_id};
    if (!dds.init())
    {
        swarm::log("node", "HATA: DDS baslatilamadi");
        return 1;
    }

    dds_ile_bagla(dds, yonetici);

    yonetici.run();
    swarm::log("node", "hazir - 3 thread calisiyor");

    if (config.node_type == swarm::NodeType::GCS)
    {
        gcs_dongusu(yonetici);
    }
    else
    {
        drone_dongusu();
    }

    swarm::log("node", "kapatiliyor");
    yonetici.stop();
    swarm::log("node", "kapandi");

    return 0;
}
