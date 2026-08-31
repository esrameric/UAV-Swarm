// ============================================================================
//  SwarmManager — sistemin merkezi koordinatörü (Singleton, thread-safe)
//
//  SINGLETON NEDİR? Bir sınıftan programda YALNIZCA BİR tane nesne olmasını
//  garanti eden tasarım deseni. Erişim tek bir noktadan (`get_instance()`)
//  yapılır. Burada gerekli çünkü peer table, komut queue'su ve görev queue'su
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
//  Buna "Meyers Singleton" denir ve elle lock yazmaktan daha güvenlidir.
// ============================================================================

//  THREAD-SAFETY (Bölüm 3.2)
//  Üç thread aynı verilere dokunduğu için paylaşılan her yapı bir MUTEX ile
//  korunur:
//    peer_table_    <- peer_mutex_     (Thread 1 yazar, Thread 3 okur)
//    command_queue_ <- command_mutex_  (Thread 2 yazar, Thread 3 okur)
//    task_queue_    <- KORUMA YOK, gerekmiyor: yalnızca Thread 3 dokunur.
//                      Tek thread'in eriştiği veriye lock koymak gereksiz
//                      maliyet ve yanlış bir "burada yarış var" sinyalidir.
#pragma once

#include <atomic>
#include <chrono>
#include <functional>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

#include "Consensus.hpp"
#include "Heartbeat.hpp"
#include "SwarmEnums.hpp"
#include "TaskAllocation.hpp"
#include "Telemetry.hpp"
#include "swarm/command.hpp"
#include "swarm/drone_state.hpp"
#include "swarm/peer_manager.hpp"
#include "swarm/task/consensus_task.hpp"
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

    // ARIZA ENJEKSİYONU (fault injection) — üretim davranışı değil, test
    // aracı. Açıkken drone consensus teklifine hiç cevap VERMEZ; yani
    // "ayakta ama sessiz" bir düğüm simüle edilir.
    //
    // Neden gerekli: Faz 6.3'ün doğrulaması gereken 5 saniyelik zaman aşımı
    // senaryosu başka türlü kurulamıyor. Bir container'ı durdurmak veya
    // duraklatmak işe yaramaz — heartbeat'i kesildiği anda (3 sn) diğer
    // düğümler onu OFFLINE sayıp oy verecekler listesinden çıkarır ve
    // oylama zaman aşımına hiç düşmez.
    bool fault_silent_consensus = false;
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
    DroneState& drone_state() { return own_state_; }
    const DroneState& drone_state() const { return own_state_; }

    // --- Consensus turu isteği (thread-safe) ---------------------------------
    //
    // Başka bir thread'den (GCS'in ana döngüsü) yeni bir oylama başlatmak
    // için kullanılır. Görev queue'suna DOKUNMAZ; yalnızca bir istek bırakır.
    // İsteği alıp queue'yu düzenlemek Task Engine thread'inin işidir.
    void request_consensus(uint32_t transaction_id, std::vector<uint8_t> voters);

    // --- Task Engine (Thread 3) ----------------------------------------------

    // Task Engine'in TEK turu. Thread 3 bunu döngüde çağırır.
    // Testlerin zamanı kontrol edebilmesi için ayrı ve public: thread
    // başlatmadan, istenen `now` değeriyle adım adım işletilebilir.
    //
    // Sırası (Bölüm 3.2):
    //   1) check_emergency()
    //   2) bekleyen komutları işle
    //   3) aktif görevi ilerlet, bittiyse sıradakine geç
    //   4) queue boşaldıysa IdleTask'a düş
    void task_engine_step(TimePoint now);

    // Komut queue'sundaki her şeyi işler: consensus oylarını aktif
    // ConsensusTask'a iletir, görev emirlerini TaskAllocationEngine'e
    // danışarak görev queue'suna dönüştürür.
    void process_pending_commands(TimePoint now);

    // Acil durum var mı? (Bölüm 3.2 — her turda ilk çalışan kontrol)
    //
    // İki koşuldan biri sağlanırsa acil durum vardır:
    //   1) Kendi bataryamız kritik seviyenin altına indi.
    //   2) Bir zamanlar duyduğumuz bir peer artık susuyor (kayıp düğüm).
    bool check_emergency(TimePoint now) const;

    static constexpr uint8_t CRITICAL_BATTERY_PERCENTAGE = 15;

    // --- Ağ interface'i (Thread 1 ve Faz 4'teki DDS okuyucuları) -------------

    // Kendi durumunu (heartbeat + telemetri) yayınlar.
    void send_self_status(TimePoint now);

    // Peer'ların canlılık durumunu tazeler: zaman aşımına uğrayanları
    // OFFLINE'a çeker.
    void update_peer_list(TimePoint now);

    // Ağdan bir heartbeat/telemetri geldiğinde çağrılır (peer_mutex korumalı).
    void on_heartbeat_received(const Heartbeat& heartbeat, TimePoint now);
    bool on_telemetry_received(const Telemetry& telemetry, TimePoint now);

    // Şu an yayınlanacak mesajları üretir. Yayıncı bağlı olmasa da
    // çağrılabilir; testler ve GCS izleme bunları doğrudan kullanır.
    Heartbeat build_heartbeat() const;
    Telemetry build_telemetry(TimePoint now);

    // Yayın kanalları. Faz 4'te FastDDSWrapper bunları dolduracak; boş
    // bırakılırlarsa send_self_status() mesajı üretir ama kimseye vermez.
    //
    // std::function: "bir fonksiyonu değişkende saklamanın" yolu. Buraya
    // lambda, serbest fonksiyon veya üye fonksiyon bağlanabilir. Sayesinde
    // SwarmManager, DDS'i hiç tanımadan yayın yapabiliyor.
    using HeartbeatPublisher = std::function<void(const Heartbeat&)>;
    using TelemetryPublisher = std::function<void(const Telemetry&)>;
    using ConsensusPublisher = std::function<void(const Consensus&)>;
    using TaskOrderPublisher = std::function<void(const TaskAllocation&)>;

    void set_heartbeat_publisher(HeartbeatPublisher publisher);
    void set_telemetry_publisher(TelemetryPublisher publisher);
    void set_consensus_publisher(ConsensusPublisher publisher);
    void set_task_allocation_publisher(TaskOrderPublisher publisher);

    // Reliable kanaldan mesaj yayınlar. Yayıncı bağlı değilse sessizce
    // hiçbir şey yapmaz.
    void publish_consensus(const Consensus& message);
    void publish_task_allocation(const TaskAllocation& order);

    // --- Sürü görüntüsü ------------------------------------------------------

    // Şu an ONLINE olan drone'ların kimlikleri (peer_mutex korumalı).
    std::vector<uint8_t> online_drone_ids() const;

    // --- Son tamamlanan oylamanın sonucu -------------------------------------
    //
    // Task Engine bir ConsensusTask'ı queue'dan çıkarırken sonucunu buraya
    // yazar. Böylece GCS (veya başka bir gözlemci), silinmiş bir task'a
    // işaretçi tutmadan sonucu öğrenebilir.
    struct ConsensusOutcome
    {
        bool valid = false;          // hiç oylama tamamlandı mı?
        uint32_t transaction_id = 0;
        ConsensusResult result = ConsensusResult::PENDING;
        bool cancelled_by_timeout = false;
    };

    ConsensusOutcome last_consensus_result() const { return last_consensus_outcome_; }

    // Aktif görevin tipi. `atomic` olduğu için başka thread'lerden (örn.
    // ana thread'in log döngüsü) güvenle okunabilir — task_queue_'nun
    // kendisine yalnızca Task Engine thread'i dokunabilir.
    TaskType current_task_type() const { return active_task_type_; }

    // --- Komut queue'su (command_mutex_ korumalı) -----------------------------

    // Ağdan gelen bir komutu queue'ya ekler. Thread 2 çağırır.
    void add_command(const Command& command);

    // Queue'nun başındaki komutu alır ve queue'dan çıkarır.
    // Dönüş: queue boşsa false (bu durumda `output` değiştirilmez).
    // Thread 3 çağırır.
    bool pop_command(Command& output);

    std::size_t command_queue_size() const;

    // --- Peer table (peer_mutex_ korumalı) -----------------------------------

    std::size_t peer_count() const;
    std::size_t online_peer_count() const;

    // --- Görev queue'su (yalnızca Thread 3) -----------------------------------
    //
    // DİKKAT: Aşağıdaki üç fonksiyon KİLİTSİZDİR. Yalnızca Task Engine
    // thread'inin içinden ya da run() çağrılmadan önce (kurulum ve testler)
    // güvenle kullanılabilir. Çalışan bir düğümde başka bir thread'den
    // çağrılırlarsa data race olur — nitekim GCS'in görev başlatması
    // bu yüzden request_consensus() üzerinden yapılıyor.

    // Queue'nun SONUNA bir görev ekler.
    void push_task(std::unique_ptr<Task> task);

    std::size_t task_queue_size() const;

    // Queue'nun başındaki (aktif) görev. Queue boşsa nullptr.
    // Sahiplik devredilmez: görev queue'ya aittir.
    Task* current_task();

    // Bekleyen TÜM görevleri iptal eder. Consensus ABORTED bittiğinde
    // (Bölüm 2/3.6) sürünün IdleTask'a dönebilmesi için queue'nun
    // boşaltılması gerekir.
    void clear_task_queue();

private:
    // Kurucu ve yıkıcı `private`: yalnızca sınıfın kendisi (get_instance)
    // örnek oluşturabilir/yok edebilir.
    SwarmManager() = default;
    ~SwarmManager() = default;

    // Acil durumda queue'yu boşaltıp FailSafe -> Landing -> Idle dizisini
    // yerleştirir. Yalnızca acil duruma İLK girişte çalışır.
    void place_emergency_tasks(TimePoint now);

    // Bir drone, GCS'ten gelen consensus TEKLİFİNE kendi oyunu üretip
    // yayınlar. Karar ölçütü şimdilik batarya seviyesidir.
    void vote_on_proposal(const Consensus& proposal);

    // Aktif görev tipini kaydeder ve değiştiyse log'a yazar.
    void record_task_type(TaskType new_type);

    // Queue'yu çalıştırmaya hazır hâle getirir: boşsa IdleTask koyar, sonra
    // baştaki görev henüz başlatılmadıysa başlatır.
    void prepare_queue(TimePoint now);

    // Queue'nun başındaki görevi başlatır: on_enter() çağırır, bayrağı kurar
    // ve tipi kaydeder. Bu üçü HER ZAMAN birlikte yapılmalı; ayrı ayrı
    // yazıldıklarında biri unutulabiliyor.
    void start_active_task(TimePoint now);

    // Bekleyen consensus isteği varsa queue'yu ona göre düzenler.
    // Yalnızca Task Engine thread'inden çağrılır.
    void apply_pending_consensus_request();

    // --- Thread gövdeleri ----------------------------------------------------

    void drone_list_loop();       // Thread 1 — heartbeat yayını + peer takibi
    void command_loop();            // Thread 2 — gelen komutların işlenmesi
    void task_engine_loop();    // Thread 3 — Task Engine


    // --- Paylaşılan durum ----------------------------------------------------

    // std::mutex ("mutual exclusion" = karşılıklı dışlama): aynı anda
    // yalnızca bir thread'in korunan veriye dokunmasını sağlayan lock.
    // Kilit alınmadan yapılan eşzamanlı okuma/yazma bir "data race" yaratır
    // ve C++'ta TANIMSIZ DAVRANIŞtır — bazen çalışır, bazen sessizce
    // yanlış sonuç verir.
    //
    // `mutable`: bu üye, `const` bir üye fonksiyon içinde bile
    // değiştirilebilir. Gerekli, çünkü peer_count() const'tur ama okumak
    // için yine de lock'u almak zorundadır.
    mutable std::mutex peer_mutex_;
    PeerManager peer_table_;

    mutable std::mutex command_mutex_;
    // std::deque: iki uçtan da hızlı ekleme/çıkarma yapılabilen queue.
    // Komutlar sona eklenir, baştan işlenir (FIFO).
    std::deque<Command> command_queue_;

    // Yalnızca Thread 3 (Task Engine) dokunduğu için mutex'i yok.
    // Görevler unique_ptr ile tutulur: queue'dan çıkan görev otomatik silinir.
    std::deque<std::unique_ptr<Task>> task_queue_;

    // Queue'nun başındaki göreve on_enter() çağrıldı mı? Bir görev aktif
    // olduğunda on_enter'ı tam bir kez çağırmak için gerekli.
    bool active_task_started_ = false;

    // Acil duruma girildi mi? Acil durum görevlerinin her turda tekrar
    // tekrar queue'ya eklenmesini engeller.
    bool in_emergency_ = false;

    // Kendi telemetri sayacımız. HİÇ SIFIRLANMAZ (Bölüm 3.5): süreç
    // kapanınca RAM'deki değer doğal olarak gider, yeniden başlayınca
    // 0'dan devam eder. Alıcı taraf bunu OFFLINE->ONLINE geçişinde
    // last_seen_seq'i sıfırlayarak karşılar.
    uint32_t telemetry_seq_num_ = 0;

    HeartbeatPublisher heartbeat_publisher_;
    TelemetryPublisher telemetry_publisher_;
    ConsensusPublisher consensus_publisher_;
    TaskOrderPublisher task_order_publisher_;

    ConsensusOutcome last_consensus_outcome_{};

    std::atomic<TaskType> active_task_type_{TaskType::INIT};

    // Bekleyen consensus isteği (istek_mutex_ korumalı).
    struct ConsensusRequest
    {
        bool present = false;
        uint32_t transaction_id = 0;
        std::vector<uint8_t> voters;
    };
    mutable std::mutex request_mutex_;
    ConsensusRequest pending_consensus_request_;

    // --- Kimlik ve kendi durumu ----------------------------------------------

    SwarmConfig config_{};
    DroneState own_state_{};

    // --- Thread yönetimi -----------------------------------------------------

    // std::atomic<bool>: birden fazla thread'in lock kullanmadan, GÜVENLE okuyup
    // yazabildiği bayrak. Sıradan bir `bool` burada data race olurdu:
    // derleyici onu bir yazmaca (register) alıp döngüden çıkarabilir ve
    // thread durma işaretini hiç görmeyebilirdi.
    std::atomic<bool> running_{false};

    std::thread drone_list_thread_;
    std::thread command_thread_;
    std::thread task_engine_thread_;

    // Thread döngülerinin tur arası bekleme süreleri.
    // Heartbeat ~10 Hz (Bölüm 3.4), diğerleri daha sık dönebilir.
    static constexpr std::chrono::milliseconds HEARTBEAT_PERIOD{100};
    static constexpr std::chrono::milliseconds COMMAND_PERIOD{20};
    static constexpr std::chrono::milliseconds TASK_ENGINE_PERIOD{20};
};

}  // namespace swarm
