// ============================================================================
//  SwarmManager — sistemin merkezi koordinatörü (Singleton, thread-safe)
//
//  SINGLETON NEDİR? Bir sınıftan programda YALNIZCA BİR tane nesne olmasını
//  garanti eden tasarım deseni. Erişim tek bir noktadan (`get_instance()`)
//  yapılır. Burada gerekli çünkü peer table, komut kuyruğu ve görev kuyruğu
//  tek bir gerçeğin kaydı olmalı: üç thread'in aynı tabloyu görmesi şart.
//
//  Singleton nasıl zorlanır?
//    1) Kurucu `private` yapılır -> dışarıdan `SwarmManager m;` yazılamaz.
//    2) Kopyalama ve taşıma `= delete` ile silinir -> kazara ikinci bir
//       kopya üretilemez.
//    3) Tek örnek `get_instance()` içinde `static` olarak tutulur.
//
//  Neden `static` yerel değişken? C++11'den beri fonksiyon içindeki static
//  değişkenlerin ilklenmesi THREAD-SAFE olmak zorundadır: iki thread aynı
//  anda `get_instance()` çağırsa bile nesne yalnızca bir kez kurulur.
//  Buna "Meyers Singleton" denir ve elle kilit yazmaktan daha güvenlidir.
// ============================================================================

//  THREAD-SAFETY (Bölüm 3.2)
//  Üç thread aynı verilere dokunduğu için paylaşılan her yapı bir MUTEX ile
//  korunur:
//    peer_table_    <- peer_mutex_     (Thread 1 yazar, Thread 3 okur)
//    command_queue_ <- command_mutex_  (Thread 2 yazar, Thread 3 okur)
//    task_queue_    <- KORUMA YOK, gerekmiyor: yalnızca Thread 3 dokunur.
//                      Tek thread'in eriştiği veriye kilit koymak gereksiz
//                      maliyet ve yanlış bir "burada yarış var" sinyalidir.
#pragma once

#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>

#include "SwarmEnums.hpp"
#include "swarm/command.hpp"
#include "swarm/drone_state.hpp"
#include "swarm/peer_manager.hpp"
#include "swarm/task/task.hpp"

namespace swarm {

// ---------------------------------------------------------------------------
//  SwarmConfig — bu düğümün kimliği
//
//  Faz 4.3'te ortam değişkenlerinden (DRONE_ID, NODE_TYPE, ROLE,
//  ROS_DOMAIN_ID) doldurulup init()'e verilecek.
// ---------------------------------------------------------------------------
struct SwarmConfig
{
    uint8_t drone_id = 0;                       // GCS = 0, drone'lar 1..3
    NodeType node_type = NodeType::DRONE;
    DroneRole role = DroneRole::SCOUT;          // yalnızca DRONE için anlamlı
    uint32_t domain_id = 42;                    // DDS domain (ROS_DOMAIN_ID)
};

class SwarmManager
{
public:
    // Tekil örneğe erişim. Referans döndürür (işaretçi değil): çağıran
    // tarafın "nullptr mı?" diye kontrol etmesi gerekmez, silmesi de
    // mümkün değildir.
    static SwarmManager& get_instance();

    // KOPYALAMA VE TAŞIMA SİLİNDİ.
    // `= delete`: "bu fonksiyon yok, kullanmaya çalışan derleme hatası alsın".
    // Bunlar silinmeseydi `SwarmManager kopya = SwarmManager::get_instance();`
    // yazımı sessizce ikinci bir SwarmManager üretir ve singleton garantisi
    // çökerdi.
    SwarmManager(const SwarmManager&) = delete;
    SwarmManager& operator=(const SwarmManager&) = delete;
    SwarmManager(SwarmManager&&) = delete;
    SwarmManager& operator=(SwarmManager&&) = delete;

    // --- Yaşam döngüsü -------------------------------------------------------

    // Bellek yapılarını hazırlar ve bu düğümün kimliğini belirler.
    // run()'dan ÖNCE çağrılmalıdır. Yeniden çağrılabilir (yeniden
    // yapılandırma); çalışan bir düğümde çağrılırsa önce stop() gerekir.
    void init(const SwarmConfig& config);

    // Bölüm 3.2'deki 3 thread'i eş zamanlı başlatır. Bloklamaz; thread'ler
    // arka planda çalışmaya devam eder.
    // Zaten çalışıyorsa hiçbir şey yapmaz.
    void run();

    // Thread'lere durma işareti verir ve hepsinin bitmesini bekler (join).
    // Zaten durmuşsa hiçbir şey yapmaz.
    void stop();

    bool is_running() const;

    const SwarmConfig& config() const { return config_; }

    // Bu düğümün kendi uçuş durumu. Hareket task'ları bunu günceller,
    // telemetri yayını buradan okur.
    DroneState& drone_state() { return kendi_durumu_; }
    const DroneState& drone_state() const { return kendi_durumu_; }

    // --- Task Engine (Thread 3) ----------------------------------------------

    // Task Engine'in TEK turu. Thread 3 bunu döngüde çağırır.
    // Testlerin zamanı kontrol edebilmesi için ayrı ve public: thread
    // başlatmadan, istenen `now` değeriyle adım adım işletilebilir.
    //
    // Sırası (Bölüm 3.2):
    //   1) check_emergency()
    //   2) bekleyen komutları işle
    //   3) aktif görevi ilerlet, bittiyse sıradakine geç
    //   4) kuyruk boşaldıysa IdleTask'a düş
    void task_engine_adimi(TimePoint now);

    // Komut kuyruğundaki her şeyi işler: consensus oylarını aktif
    // ConsensusTask'a iletir, görev emirlerini TaskAllocationEngine'e
    // danışarak görev kuyruğuna dönüştürür.
    void bekleyen_komutlari_isle(TimePoint now);

    // Acil durum var mı? (Bölüm 3.2 — her turda ilk çalışan kontrol)
    bool check_emergency(TimePoint now) const;

    // --- Komut kuyruğu (command_mutex_ korumalı) -----------------------------

    // Ağdan gelen bir komutu kuyruğa ekler. Thread 2 çağırır.
    void add_command(const Command& komut);

    // Kuyruğun başındaki komutu alır ve kuyruktan çıkarır.
    // Dönüş: kuyruk boşsa false (bu durumda `cikti` değiştirilmez).
    // Thread 3 çağırır.
    bool pop_command(Command& cikti);

    std::size_t command_queue_size() const;

    // --- Peer table (peer_mutex_ korumalı) -----------------------------------

    std::size_t peer_count() const;
    std::size_t online_peer_count() const;

    // --- Görev kuyruğu (yalnızca Thread 3) -----------------------------------

    // Kuyruğun SONUNA bir görev ekler.
    void push_task(std::unique_ptr<Task> gorev);

    std::size_t task_queue_size() const;

    // Kuyruğun başındaki (aktif) görev. Kuyruk boşsa nullptr.
    // Sahiplik devredilmez: görev kuyruğa aittir.
    Task* current_task();

    // Bekleyen TÜM görevleri iptal eder. Consensus ABORTED bittiğinde
    // (Bölüm 2/3.6) sürünün IdleTask'a dönebilmesi için kuyruğun
    // boşaltılması gerekir.
    void clear_task_queue();

private:
    // Kurucu ve yıkıcı `private`: yalnızca sınıfın kendisi (get_instance)
    // örnek oluşturabilir/yok edebilir.
    SwarmManager() = default;
    ~SwarmManager() = default;

    // Acil durumda kuyruğu boşaltıp FailSafe -> Landing -> Idle dizisini
    // yerleştirir. Yalnızca acil duruma İLK girişte çalışır.
    void acil_durum_gorevlerini_yerlestir(TimePoint now);

    // --- Thread gövdeleri ----------------------------------------------------

    void drone_list_dongusu();       // Thread 1 — heartbeat yayını + peer takibi
    void komut_dongusu();            // Thread 2 — gelen komutların işlenmesi
    void komut_yurutme_dongusu();    // Thread 3 — Task Engine

    // --- Faz 3.5'te doldurulacak yardımcılar ---------------------------------

    // Kendi Heartbeat'ini yayınlar (Thread 1).
    void send_self_status(TimePoint now);

    // Gelen heartbeat'lerle peer table'ı günceller (Thread 1).
    void update_peer_list(TimePoint now);

    // --- Paylaşılan durum ----------------------------------------------------

    // std::mutex ("mutual exclusion" = karşılıklı dışlama): aynı anda
    // yalnızca bir thread'in korunan veriye dokunmasını sağlayan kilit.
    // Kilit alınmadan yapılan eşzamanlı okuma/yazma "veri yarışı" (data race)
    // yaratır ve C++'ta TANIMSIZ DAVRANIŞtır — bazen çalışır, bazen sessizce
    // yanlış sonuç verir.
    //
    // `mutable`: bu üye, `const` bir üye fonksiyon içinde bile
    // değiştirilebilir. Gerekli, çünkü peer_count() const'tur ama okumak
    // için yine de kilidi kilitlemek zorundadır.
    mutable std::mutex peer_mutex_;
    PeerManager peer_table_;

    mutable std::mutex command_mutex_;
    // std::deque: iki uçtan da hızlı ekleme/çıkarma yapılabilen kuyruk.
    // Komutlar sona eklenir, baştan işlenir (FIFO).
    std::deque<Command> command_queue_;

    // Yalnızca Thread 3 (Task Engine) dokunduğu için mutex'i yok.
    // Görevler unique_ptr ile tutulur: kuyruktan çıkan görev otomatik silinir.
    std::deque<std::unique_ptr<Task>> task_queue_;

    // Kuyruğun başındaki göreve on_enter() çağrıldı mı? Bir görev aktif
    // olduğunda on_enter'ı tam bir kez çağırmak için gerekli.
    bool aktif_gorev_baslatildi_ = false;

    // Acil duruma girildi mi? Acil durum görevlerinin her turda tekrar
    // tekrar kuyruğa eklenmesini engeller.
    bool acil_durumda_ = false;

    // --- Kimlik ve kendi durumu ----------------------------------------------

    SwarmConfig config_{};
    DroneState kendi_durumu_{};

    // --- Thread yönetimi -----------------------------------------------------

    // std::atomic<bool>: birden fazla thread'in kilitsiz, GÜVENLE okuyup
    // yazabildiği bayrak. Sıradan bir `bool` burada veri yarışı olurdu:
    // derleyici onu bir yazmaca (register) alıp döngüden çıkarabilir ve
    // thread durma işaretini hiç görmeyebilirdi.
    std::atomic<bool> calisiyor_{false};

    std::thread drone_list_threadi_;
    std::thread komut_threadi_;
    std::thread komut_yurutme_threadi_;

    // Thread döngülerinin tur arası bekleme süreleri.
    // Heartbeat ~10 Hz (Bölüm 3.4), diğerleri daha sık dönebilir.
    static constexpr std::chrono::milliseconds HEARTBEAT_PERIYODU{100};
    static constexpr std::chrono::milliseconds KOMUT_PERIYODU{20};
    static constexpr std::chrono::milliseconds TASK_ENGINE_PERIYODU{20};
};

}  // namespace swarm
