#include "swarm/swarm_manager.hpp"

#include <string>
#include <utility>

#include "swarm/task/consensus_task.hpp"
#include "swarm/task/fail_safe_task.hpp"
#include "swarm/task/idle_task.hpp"
#include "swarm/task/landing_task.hpp"
#include "swarm/enum_names.hpp"
#include "swarm/log.hpp"
#include "swarm/task_allocation_engine.hpp"

namespace swarm {

SwarmManager& SwarmManager::get_instance()
{
    // FONKSİYON İÇİ STATIC: bu değişken fonksiyon ilk çağrıldığında BİR KEZ
    // kurulur ve program bitene kadar yaşar. Sonraki çağrılar aynı nesneyi
    // döndürür. C++11'den beri bu ilkleme thread-safe'tir; iki thread aynı
    // anda girse bile nesne yalnızca bir kez kurulur.
    static SwarmManager single_instance;
    return single_instance;
}


// ---------------------------------------------------------------------------
//  Yaşam döngüsü
// ---------------------------------------------------------------------------

void SwarmManager::init(const SwarmConfig& config)
{
    config_ = config;

    // Yeniden yapılandırmada eski durumdan kalıntı kalmasın.
    own_state_ = DroneState{};
    last_consensus_outcome_ = ConsensusOutcome{};
    active_task_started_ = false;
    in_emergency_ = false;
    active_task_type_ = TaskType::INIT;
    {
        const std::lock_guard<std::mutex> lock(request_mutex_);
        pending_consensus_request_ = ConsensusRequest{};
    }

    {
        const std::lock_guard<std::mutex> lock(peer_mutex_);
        peer_table_ = PeerManager{};
    }
    {
        const std::lock_guard<std::mutex> lock(command_mutex_);
        command_queue_.clear();
    }
    task_queue_.clear();
}

void SwarmManager::run()
{
    if (running_)
    {
        return;  // zaten çalışıyor
    }

    running_ = true;

    // Üç thread eş zamanlı başlar. Ağ dinlemek (I/O ağırlıklı) ile görev
    // yürütmek (hesap ağırlıklı) ayrı thread'lerde olduğu için biri
    // diğerini bekletmez (Bölüm 2).
    //
    // `this` yakalaması: lambda, SwarmManager'ın üye fonksiyonunu
    // çağırabilmek için nesnenin kendisine erişmek zorunda.
    drone_list_thread_ = std::thread([this]() { drone_list_loop(); });
    command_thread_ = std::thread([this]() { command_loop(); });
    task_engine_thread_ = std::thread([this]() { task_engine_loop(); });
}

void SwarmManager::stop()
{
    if (!running_)
    {
        return;
    }

    // Önce durma işaretini veriyoruz; thread'ler döngü başında bunu görüp
    // kendiliğinden çıkacak.
    running_ = false;

    // joinable(): thread gerçekten başlatılmış ve henüz join edilmemiş mi?
    // join edilmemiş bir std::thread yok edilirse program std::terminate
    // ile aniden sonlanır — bu yüzden hepsini bekliyoruz.
    if (drone_list_thread_.joinable())
    {
        drone_list_thread_.join();
    }
    if (command_thread_.joinable())
    {
        command_thread_.join();
    }
    if (task_engine_thread_.joinable())
    {
        task_engine_thread_.join();
    }
}

bool SwarmManager::is_running() const
{
    return running_;
}

// ---------------------------------------------------------------------------
//  Thread 1 — Drone List Döngüsü  (UDP / Multicast)
//
//  Kendi durumunu yayınlar ve gelen heartbeat'lere göre peer table'ı
//  günceller. ~10 Hz.
// ---------------------------------------------------------------------------

void SwarmManager::drone_list_loop()
{
    while (running_)
    {
        const TimePoint current_time = std::chrono::steady_clock::now();

        send_self_status(current_time);
        update_peer_list(current_time);

        std::this_thread::sleep_for(HEARTBEAT_PERIOD);
    }
}

// ---------------------------------------------------------------------------
//  Thread 2 — Komut Döngüsü  (TCP / Reliable)
//
//  YKİ'den ve diğer drone'lardan gelen komutları ağdan alıp command_queue'ya
//  ekler. Queue'yu BOŞALTMAZ — tüketmek Thread 3'ün işidir; böylece görev
//  queue'suna tek bir thread dokunmuş olur.
//
//  Ağ tarafı Faz 4'te FastDDSWrapper ile bağlanacak; DDS okuyucuları gelen
//  mesajları add_command() ile bu queue'ya koyacak.
// ---------------------------------------------------------------------------

void SwarmManager::command_loop()
{
    while (running_)
    {
        std::this_thread::sleep_for(COMMAND_PERIOD);
    }
}

// ---------------------------------------------------------------------------
//  Thread 3 — Komut Yürütme Döngüsü  (Task Engine)
// ---------------------------------------------------------------------------

void SwarmManager::task_engine_loop()
{
    while (running_)
    {
        task_engine_step(std::chrono::steady_clock::now());
        std::this_thread::sleep_for(TASK_ENGINE_PERIOD);
    }
}

void SwarmManager::request_consensus(uint32_t transaction_id, std::vector<uint8_t> voters)
{
    const std::lock_guard<std::mutex> lock(request_mutex_);
    pending_consensus_request_.present = true;
    pending_consensus_request_.transaction_id = transaction_id;
    pending_consensus_request_.voters = std::move(voters);
}

void SwarmManager::apply_pending_consensus_request()
{
    ConsensusRequest request;
    {
        const std::lock_guard<std::mutex> lock(request_mutex_);
        if (!pending_consensus_request_.present)
        {
            return;
        }
        // İsteği lock altında alıp bayrağı hemen indiriyoruz; queue
        // düzenlemesini lock DIŞINDA yapıyoruz ki lock uzun tutulmasın.
        request = std::move(pending_consensus_request_);
        pending_consensus_request_ = ConsensusRequest{};
    }

    // Yeni oylama her şeyin önüne geçer: bekleyen görevler iptal edilir.
    task_queue_.clear();

    // Bu bayrağı sıfırlamak ŞART: aksi halde Task Engine yeni görevi
    // "zaten başlatılmış" sanar, on_enter() çağrılmaz ve ConsensusTask'ın
    // zaman aşımı sayacı hiç başlamadığı için oylama anında timeout'a düşer.
    active_task_started_ = false;

    task_queue_.push_back(
            std::make_unique<ConsensusTask>(request.transaction_id, request.voters));
}

void SwarmManager::task_engine_step(TimePoint now)
{
    // --- 0) Başka thread'den gelen consensus isteği var mı? ------------------
    apply_pending_consensus_request();

    // --- 1) Önce acil durum kontrolü (Bölüm 3.2) -----------------------------
    if (check_emergency(now))
    {
        place_emergency_tasks(now);
    }
    else
    {
        in_emergency_ = false;
    }

    // --- 2) Aktif görevi komutlardan ÖNCE başlat -----------------------------
    //
    // SIRA KRİTİK. Bir görev, kendisine yönelik ilk komutu işlemeden ÖNCE
    // on_enter() ile başlatılmış olmalı.
    //
    // Neden: ConsensusTask::on_enter() zaman aşımı sayacını kurar ve sonucu
    // PENDING'e çeker. Oylar on_enter'dan önce işlenirse, aynı turda gelen
    // tüm ACK'lerle COMMITTED'e ulaşan bir oylamanın sonucu hemen ardından
    // on_enter tarafından siliniyordu; oylama PENDING'de kalıp 5 saniye
    // sonra hatalı biçimde zaman aşımına düşüyordu. Bu, drone'lar oy vermiş
    // olmasına rağmen görevin iptal edilmesi demekti.
    prepare_queue(now);

    // --- 3) Bekleyen komutları işle ------------------------------------------
    process_pending_commands(now);

    // Komut işleme queue'yu değiştirmiş olabilir: yeni bir görev emri
    // IdleTask'ı yerinden eder. Devralan görev de çalıştırılmadan önce
    // başlatılmalı.
    prepare_queue(now);

    Task* active_task = task_queue_.front().get();

    // --- 4) Aktif görevi ilerlet ---------------------------------------------
    active_task->run(now);

    if (!active_task->is_finished())
    {
        return;
    }

    // --- 5) Görev bitti: consensus iptali mi? --------------------------------
    // dynamic_cast: taban sınıf işaretçisinin gerçekte hangi child'a ait
    // olduğunu ÇALIŞMA ZAMANINDA sorar. Aradığımız tip değilse nullptr
    // döner. Burada gerekli, çünkü "görev bitti" sinyalinin anlamı
    // ConsensusTask için özeldir.
    const ConsensusTask* consensus = dynamic_cast<const ConsensusTask*>(active_task);
    const bool task_should_be_cancelled = (consensus != nullptr) && consensus->mission_should_abort();

    if (consensus != nullptr)
    {
        // Sonucu saklıyoruz: task birazdan silinecek, ama GCS gibi
        // gözlemcilerin sonucu öğrenmesi gerekiyor.
        last_consensus_outcome_.valid = true;
        last_consensus_outcome_.transaction_id = consensus->transaction_id();
        last_consensus_outcome_.result = consensus->result();
        last_consensus_outcome_.cancelled_by_timeout = consensus->cancelled_by_timeout();

        std::string result_name = "PENDING";
        if (consensus->result() == ConsensusResult::COMMITTED)
        {
            result_name = "COMMITTED";
        }
        else if (consensus->result() == ConsensusResult::ABORTED)
        {
            result_name = consensus->cancelled_by_timeout() ? "ABORTED_TIMEOUT" : "ABORTED_NACK";
        }
        log("consensus", "sonuc tx=" + std::to_string(consensus->transaction_id()) +
                         " " + result_name);
    }

    active_task->on_exit();
    task_queue_.pop_front();
    active_task_started_ = false;

    if (task_should_be_cancelled)
    {
        // Bölüm 2/3.6: oylama başarısızsa TÜM görev iptal edilir ve sürü
        // IdleTask'a döner. Bir sonraki turda queue boş bulunacağı için
        // IdleTask kendiliğinden yerleşir.
        task_queue_.clear();
        return;
    }

    // Sıradaki görev varsa hemen başlat.
    start_active_task(now);
}

void SwarmManager::prepare_queue(TimePoint now)
{
    // Kuyruk boşaldıysa IdleTask'a düşülür (Bölüm 3.2).
    if (task_queue_.empty())
    {
        task_queue_.push_back(std::make_unique<IdleTask>(own_state_));
        active_task_started_ = false;
    }

    if (!active_task_started_)
    {
        start_active_task(now);
    }
}

void SwarmManager::start_active_task(TimePoint now)
{
    if (task_queue_.empty())
    {
        return;
    }

    Task& task = *task_queue_.front();
    task.on_enter(now);
    active_task_started_ = true;
    record_task_type(task.get_type());
}

void SwarmManager::process_pending_commands(TimePoint now)
{
    Command command;
    while (pop_command(command))
    {
        switch (command.type)
        {
            case CommandType::CONSENSUS:
            {
                // Kendi yayınımızı geri duymuş olabiliriz.
                if (command.consensus.sender_id() == config_.drone_id)
                {
                    break;
                }

                // TEKLİF mi, OY mu? Teklifte vote alanı PENDING'dir
                // ("henüz oy yok"); oy mesajlarında ACK veya NACK olur.
                // Bir drone teklif aldığında kendi oyunu üretip yayınlar.
                if (command.consensus.vote() == Vote::PENDING)
                {
                    if (config_.node_type == NodeType::DRONE)
                    {
                        vote_on_proposal(command.consensus);
                    }
                    break;
                }

                // Oy yalnızca AKTİF ConsensusTask'ı ilgilendirir ve
                // transaction_id'si tutmalıdır.
                ConsensusTask* active_voting = nullptr;
                if (!task_queue_.empty())
                {
                    active_voting = dynamic_cast<ConsensusTask*>(task_queue_.front().get());
                }

                if (active_voting != nullptr &&
                    active_voting->transaction_id() == command.consensus.transaction_id())
                {
                    active_voting->on_vote(command.consensus.sender_id(), command.consensus.vote());
                }
                break;
            }

            case CommandType::TASK_ALLOCATION:
            {
                std::unique_ptr<Task> new_task = TaskAllocationEngine::create_task(
                        command.task_allocation, config_, own_state_);

                if (new_task == nullptr)
                {
                    break;  // emir bu düğümü ilgilendirmiyor
                }

                // Yeni görev emri geldiğinde IdleTask'ta beklemeye devam
                // etmenin anlamı yok: boşta bekleyen görevi bırakıp yeni
                // göreve geçiyoruz.
                if (!task_queue_.empty() &&
                    task_queue_.front()->get_type() == TaskType::IDLE)
                {
                    task_queue_.front()->on_exit();
                    task_queue_.pop_front();
                    active_task_started_ = false;
                }

                task_queue_.push_back(std::move(new_task));
                break;
            }
        }
    }

    (void)now;  // şu an kullanılmıyor; imza tutarlılığı için duruyor
}

void SwarmManager::record_task_type(TaskType new_type)
{
    const TaskType previous = active_task_type_;
    if (previous == new_type)
    {
        return;
    }

    active_task_type_ = new_type;
    log("task", std::string("gecis: ") + task_type_name(previous) + " -> " + task_type_name(new_type));
}

void SwarmManager::vote_on_proposal(const Consensus& proposal)
{
    // Arıza enjeksiyonu: "ayakta ama sessiz" düğüm simülasyonu (bkz.
    // SwarmConfig::fault_silent_consensus). Heartbeat yayınına devam
    // ediyoruz — yani diğerleri bizi ONLINE görüyor — ama oy vermiyoruz.
    if (config_.fault_silent_consensus)
    {
        log("consensus", "ARIZA SIMULASYONU: tx=" +
                         std::to_string(proposal.transaction_id()) + " icin oy VERILMIYOR");
        return;
    }

    // Karar ölçütü: göreve çıkacak kadar bataryamız var mı?
    // (Bölüm 3.6: "Her drone kendi durumunu kontrol eder.")
    const Vote decision = (own_state_.battery < CRITICAL_BATTERY_PERCENTAGE)
            ? Vote::NACK
            : Vote::ACK;

    Consensus vote;
    vote.transaction_id(proposal.transaction_id());
    vote.sender_id(config_.drone_id);
    vote.vote(decision);
    vote.seq_num(proposal.seq_num());

    log("consensus", "oy veriliyor tx=" + std::to_string(proposal.transaction_id()) +
                     " vote=" + (decision == Vote::ACK ? "ACK" : "NACK"));

    publish_consensus(vote);
}

void SwarmManager::place_emergency_tasks(TimePoint)
{
    if (in_emergency_)
    {
        return;  // zaten acil durum görevleri queue'da
    }

    in_emergency_ = true;
    log("emergency", "acil durum tespit edildi - FailSafe/Landing dizisine geciliyor");

    // Ne yapıyor olursak olalım bırakıp güvenli diziye geçiyoruz:
    // önce dur ve değerlendir, sonra in, sonra boşta bekle.
    task_queue_.clear();
    active_task_started_ = false;

    task_queue_.push_back(std::make_unique<FailSafeTask>(own_state_));
    task_queue_.push_back(std::make_unique<LandingTask>(own_state_));
}

// ---------------------------------------------------------------------------
//  check_emergency — her Task Engine turunda İLK çalışan kontrol
// ---------------------------------------------------------------------------

bool SwarmManager::check_emergency(TimePoint) const
{
    // 1) Kendi bataryamız kritik mi?
    if (own_state_.battery < CRITICAL_BATTERY_PERCENTAGE)
    {
        return true;
    }

    // 2) Bir zamanlar duyduğumuz bir peer artık susuyor mu?
    //
    // Peer table'a yalnızca heartbeat'ini duyduğumuz düğümler girer.
    // Dolayısıyla OFFLINE bir kayıt "önceden tanıyorduk, şimdi kayıp"
    // demektir — hiç ayağa kalkmamış bir drone burada görünmez, çünkü
    // tabloya hiç eklenmemiştir. Bu ayrım önemli: Bölüm 2'deki non-blocking
    // keşif stratejisi "hiç gelmeyen" drone'u acil durum saymaz, ama
    // "gelip kaybolan" drone'u sayar.
    const std::lock_guard<std::mutex> lock(peer_mutex_);
    return peer_table_.offline_peer_count() > 0;
}

// ---------------------------------------------------------------------------
//  Kendi durumunu yayınlama (Thread 1)
// ---------------------------------------------------------------------------

Heartbeat SwarmManager::build_heartbeat() const
{
    Heartbeat heartbeat;
    heartbeat.drone_id(config_.drone_id);
    heartbeat.node_type(config_.node_type);
    heartbeat.role(config_.role);

    // "Şu an ne yapıyorum" = aktif Task'ın tipi (Bölüm 3.5).
    // task_queue_ boşsa henüz bir görev başlamamıştır: INIT bildiriyoruz.
    heartbeat.current_task(
            task_queue_.empty() ? TaskType::INIT : task_queue_.front()->get_type());

    return heartbeat;
}

Telemetry SwarmManager::build_telemetry(TimePoint now)
{
    // Sayaç her yayında bir artar ve HİÇ SIFIRLANMAZ. Alıcı taraf
    // bayatlığı bununla ölçer (Bölüm 3.5).
    ++telemetry_seq_num_;

    Telemetry telemetry;
    telemetry.drone_id(config_.drone_id);
    telemetry.seq_num(telemetry_seq_num_);

    telemetry.x(own_state_.x);
    telemetry.y(own_state_.y);
    telemetry.z(own_state_.z);
    telemetry.vx(own_state_.vx);
    telemetry.vy(own_state_.vy);
    telemetry.vz(own_state_.vz);
    telemetry.battery(own_state_.battery);

    // timestamp YALNIZCA bilgi/log amaçlı (Bölüm 3.5); bayatlık kararında
    // kullanılmaz. Duvar saati (system_clock) burada uygundur, çünkü
    // amaç insan tarafından okunabilir bir zaman damgası vermek.
    const auto epoch_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch());
    telemetry.timestamp(static_cast<uint64_t>(epoch_ms.count()));

    (void)now;
    return telemetry;
}

void SwarmManager::send_self_status(TimePoint now)
{
    // Yayıncı bağlı değilse (Faz 4 öncesi veya testte) mesajı üretmenin
    // maliyetine girmiyoruz.
    if (heartbeat_publisher_)
    {
        heartbeat_publisher_(build_heartbeat());
    }

    if (telemetry_publisher_)
    {
        telemetry_publisher_(build_telemetry(now));
    }
}

void SwarmManager::set_heartbeat_publisher(HeartbeatPublisher publisher)
{
    heartbeat_publisher_ = std::move(publisher);
}

void SwarmManager::set_telemetry_publisher(TelemetryPublisher publisher)
{
    telemetry_publisher_ = std::move(publisher);
}

void SwarmManager::set_consensus_publisher(ConsensusPublisher publisher)
{
    consensus_publisher_ = std::move(publisher);
}

void SwarmManager::set_task_allocation_publisher(TaskOrderPublisher publisher)
{
    task_order_publisher_ = std::move(publisher);
}

void SwarmManager::publish_consensus(const Consensus& message)
{
    if (consensus_publisher_)
    {
        consensus_publisher_(message);
    }
}

void SwarmManager::publish_task_allocation(const TaskAllocation& order)
{
    if (task_order_publisher_)
    {
        task_order_publisher_(order);
    }
}

std::vector<uint8_t> SwarmManager::online_drone_ids() const
{
    const std::lock_guard<std::mutex> lock(peer_mutex_);
    return peer_table_.online_drone_ids();
}

// ---------------------------------------------------------------------------
//  Peer table güncelleme (Thread 1)
// ---------------------------------------------------------------------------

void SwarmManager::update_peer_list(TimePoint now)
{
    const std::lock_guard<std::mutex> lock(peer_mutex_);

    const std::size_t previous_offline = peer_table_.offline_peer_count();
    peer_table_.refresh_status(now);
    const std::size_t current_offline = peer_table_.offline_peer_count();

    if (current_offline > previous_offline)
    {
        log("peer", "kayip peer tespit edildi (offline sayisi=" +
                    std::to_string(current_offline) + ")");
    }
}

void SwarmManager::on_heartbeat_received(const Heartbeat& heartbeat, TimePoint now)
{
    // Kendi yayınımızı geri duyabiliriz (multicast); kendimizi peer
    // tablomuza eklemenin anlamı yok.
    if (heartbeat.drone_id() == config_.drone_id)
    {
        return;
    }

    const std::lock_guard<std::mutex> lock(peer_mutex_);

    const bool was_known_before = (peer_table_.find(heartbeat.drone_id()) != nullptr);
    const bool was_online_before =
            (peer_table_.status_of(heartbeat.drone_id()) == PeerStatus::ONLINE);

    peer_table_.on_heartbeat(heartbeat, now);

    if (!was_known_before)
    {
        const std::string role_text = (heartbeat.node_type() == NodeType::DRONE)
                ? drone_role_name(heartbeat.role())
                : "-";
        log("peer", "yeni peer: id=" + std::to_string(heartbeat.drone_id()) +
                    " type=" + node_type_name(heartbeat.node_type()) +
                    " role=" + role_text);
    }
    else if (!was_online_before)
    {
        log("peer", "geri dondu: id=" + std::to_string(heartbeat.drone_id()) +
                    " (seq takibi sifirlandi)");
    }
}

bool SwarmManager::on_telemetry_received(const Telemetry& telemetry, TimePoint now)
{
    if (telemetry.drone_id() == config_.drone_id)
    {
        return false;
    }

    const std::lock_guard<std::mutex> lock(peer_mutex_);

    bool new_stream = false;
    const bool accepted = peer_table_.on_telemetry(telemetry, now, &new_stream);

    if (accepted && new_stream)
    {
        // Sadece akış başlarken bir kez: 20-50 Hz'lik akışı loglamak
        // çıktıyı okunamaz hale getirirdi.
        log("telemetry", "id=" + std::to_string(telemetry.drone_id()) +
                         " akisi basladi seq=" + std::to_string(telemetry.seq_num()));
    }

    return accepted;
}

// ---------------------------------------------------------------------------
//  Komut queue'su
// ---------------------------------------------------------------------------

void SwarmManager::add_command(const Command& command)
{
    // std::lock_guard: RAII tabanlı lock. Kurulduğu anda mutex'i kilitler,
    // kapsam (scope) bittiğinde OTOMATİK olarak açar — fonksiyondan erken
    // return edilse veya istisna atılsa bile. Elle lock()/unlock() yazmak,
    // bir yolda unlock'u unutup tüm programı lock'lu bırakma riski taşır.
    const std::lock_guard<std::mutex> lock(command_mutex_);
    command_queue_.push_back(command);
}

bool SwarmManager::pop_command(Command& output)
{
    const std::lock_guard<std::mutex> lock(command_mutex_);

    if (command_queue_.empty())
    {
        return false;
    }

    output = command_queue_.front();
    command_queue_.pop_front();
    return true;
}

std::size_t SwarmManager::command_queue_size() const
{
    const std::lock_guard<std::mutex> lock(command_mutex_);
    return command_queue_.size();
}

// ---------------------------------------------------------------------------
//  Peer table
// ---------------------------------------------------------------------------

std::size_t SwarmManager::peer_count() const
{
    const std::lock_guard<std::mutex> lock(peer_mutex_);
    return peer_table_.peer_count();
}

std::size_t SwarmManager::online_peer_count() const
{
    const std::lock_guard<std::mutex> lock(peer_mutex_);
    return peer_table_.online_peer_count();
}

// ---------------------------------------------------------------------------
//  Görev queue'su  (lock yok: yalnızca Task Engine thread'i dokunur)
// ---------------------------------------------------------------------------

void SwarmManager::push_task(std::unique_ptr<Task> task)
{
    // std::move: unique_ptr KOPYALANAMAZ (tek sahiplik kuralı), yalnızca
    // TAŞINABİLİR. std::move, "bu nesnenin sahipliğini devrediyorum" demenin
    // yoludur; taşımadan sonra çağıranın elindeki işaretçi boşalır.
    task_queue_.push_back(std::move(task));
}

std::size_t SwarmManager::task_queue_size() const
{
    return task_queue_.size();
}

Task* SwarmManager::current_task()
{
    if (task_queue_.empty())
    {
        return nullptr;
    }
    // .get(): unique_ptr'ın işaret ettiği ham adresi verir ama SAHİPLİĞİ
    // devretmez. Çağıran bu işaretçiyi silmemelidir.
    return task_queue_.front().get();
}

void SwarmManager::clear_task_queue()
{
    // deque temizlenince içindeki unique_ptr'lar da yok olur; işaret
    // ettikleri Task nesneleri otomatik silinir (RAII).
    task_queue_.clear();
}

}  // namespace swarm
