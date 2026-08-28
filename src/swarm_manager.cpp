#include "swarm/swarm_manager.hpp"

#include <utility>

#include "swarm/task/consensus_task.hpp"
#include "swarm/task/fail_safe_task.hpp"
#include "swarm/task/idle_task.hpp"
#include "swarm/task/landing_task.hpp"
#include "swarm/task_allocation_engine.hpp"

namespace swarm {

SwarmManager& SwarmManager::get_instance()
{
    // FONKSİYON İÇİ STATIC: bu değişken fonksiyon ilk çağrıldığında BİR KEZ
    // kurulur ve program bitene kadar yaşar. Sonraki çağrılar aynı nesneyi
    // döndürür. C++11'den beri bu ilkleme thread-safe'tir; iki thread aynı
    // anda girse bile nesne yalnızca bir kez kurulur.
    static SwarmManager tek_ornek;
    return tek_ornek;
}


// ---------------------------------------------------------------------------
//  Yaşam döngüsü
// ---------------------------------------------------------------------------

void SwarmManager::init(const SwarmConfig& config)
{
    config_ = config;

    // Yeniden yapılandırmada eski durumdan kalıntı kalmasın.
    kendi_durumu_ = DroneState{};
    son_consensus_sonucu_ = ConsensusSonucu{};
    aktif_gorev_baslatildi_ = false;
    acil_durumda_ = false;

    {
        const std::lock_guard<std::mutex> kilit(peer_mutex_);
        peer_table_ = PeerManager{};
    }
    {
        const std::lock_guard<std::mutex> kilit(command_mutex_);
        command_queue_.clear();
    }
    task_queue_.clear();
}

void SwarmManager::run()
{
    if (calisiyor_)
    {
        return;  // zaten çalışıyor
    }

    calisiyor_ = true;

    // Üç thread eş zamanlı başlar. Ağ dinlemek (I/O ağırlıklı) ile görev
    // yürütmek (hesap ağırlıklı) ayrı thread'lerde olduğu için biri
    // diğerini bekletmez (Bölüm 2).
    //
    // `this` yakalaması: lambda, SwarmManager'ın üye fonksiyonunu
    // çağırabilmek için nesnenin kendisine erişmek zorunda.
    drone_list_threadi_ = std::thread([this]() { drone_list_dongusu(); });
    komut_threadi_ = std::thread([this]() { komut_dongusu(); });
    komut_yurutme_threadi_ = std::thread([this]() { komut_yurutme_dongusu(); });
}

void SwarmManager::stop()
{
    if (!calisiyor_)
    {
        return;
    }

    // Önce durma işaretini veriyoruz; thread'ler döngü başında bunu görüp
    // kendiliğinden çıkacak.
    calisiyor_ = false;

    // joinable(): thread gerçekten başlatılmış ve henüz join edilmemiş mi?
    // join edilmemiş bir std::thread yok edilirse program std::terminate
    // ile aniden sonlanır — bu yüzden hepsini bekliyoruz.
    if (drone_list_threadi_.joinable())
    {
        drone_list_threadi_.join();
    }
    if (komut_threadi_.joinable())
    {
        komut_threadi_.join();
    }
    if (komut_yurutme_threadi_.joinable())
    {
        komut_yurutme_threadi_.join();
    }
}

bool SwarmManager::is_running() const
{
    return calisiyor_;
}

// ---------------------------------------------------------------------------
//  Thread 1 — Drone List Döngüsü  (UDP / Multicast)
//
//  Kendi durumunu yayınlar ve gelen heartbeat'lere göre peer table'ı
//  günceller. ~10 Hz.
// ---------------------------------------------------------------------------

void SwarmManager::drone_list_dongusu()
{
    while (calisiyor_)
    {
        const TimePoint simdi = std::chrono::steady_clock::now();

        send_self_status(simdi);
        update_peer_list(simdi);

        std::this_thread::sleep_for(HEARTBEAT_PERIYODU);
    }
}

// ---------------------------------------------------------------------------
//  Thread 2 — Komut Döngüsü  (TCP / Reliable)
//
//  YKİ'den ve diğer drone'lardan gelen komutları ağdan alıp command_queue'ya
//  ekler. Kuyruğu BOŞALTMAZ — tüketmek Thread 3'ün işidir; böylece görev
//  kuyruğuna tek bir thread dokunmuş olur.
//
//  Ağ tarafı Faz 4'te FastDDSWrapper ile bağlanacak; DDS okuyucuları gelen
//  mesajları add_command() ile bu kuyruğa koyacak.
// ---------------------------------------------------------------------------

void SwarmManager::komut_dongusu()
{
    while (calisiyor_)
    {
        std::this_thread::sleep_for(KOMUT_PERIYODU);
    }
}

// ---------------------------------------------------------------------------
//  Thread 3 — Komut Yürütme Döngüsü  (Task Engine)
// ---------------------------------------------------------------------------

void SwarmManager::komut_yurutme_dongusu()
{
    while (calisiyor_)
    {
        task_engine_adimi(std::chrono::steady_clock::now());
        std::this_thread::sleep_for(TASK_ENGINE_PERIYODU);
    }
}

void SwarmManager::task_engine_adimi(TimePoint now)
{
    // --- 1) Önce acil durum kontrolü (Bölüm 3.2) -----------------------------
    if (check_emergency(now))
    {
        acil_durum_gorevlerini_yerlestir(now);
    }
    else
    {
        acil_durumda_ = false;
    }

    // --- 2) Bekleyen komutları işle ------------------------------------------
    bekleyen_komutlari_isle(now);

    // --- 3) Kuyruk boşsa IdleTask'a düş --------------------------------------
    if (task_queue_.empty())
    {
        task_queue_.push_back(std::make_unique<IdleTask>(kendi_durumu_));
        aktif_gorev_baslatildi_ = false;
    }

    Task* aktif_gorev = task_queue_.front().get();

    // Bir görev aktif olduğunda on_enter() tam bir kez çağrılmalı.
    if (!aktif_gorev_baslatildi_)
    {
        aktif_gorev->on_enter(now);
        aktif_gorev_baslatildi_ = true;
    }

    // --- 4) Aktif görevi ilerlet ---------------------------------------------
    aktif_gorev->run(now);

    if (!aktif_gorev->is_finished())
    {
        return;
    }

    // --- 5) Görev bitti: consensus iptali mi? --------------------------------
    // dynamic_cast: taban sınıf işaretçisinin gerçekte hangi child'a ait
    // olduğunu ÇALIŞMA ZAMANINDA sorar. Aradığımız tip değilse nullptr
    // döner. Burada gerekli, çünkü "görev bitti" sinyalinin anlamı
    // ConsensusTask için özeldir.
    const ConsensusTask* consensus = dynamic_cast<const ConsensusTask*>(aktif_gorev);
    const bool gorev_iptal_edilmeli = (consensus != nullptr) && consensus->mission_should_abort();

    if (consensus != nullptr)
    {
        // Sonucu saklıyoruz: task birazdan silinecek, ama GCS gibi
        // gözlemcilerin sonucu öğrenmesi gerekiyor.
        son_consensus_sonucu_.gecerli = true;
        son_consensus_sonucu_.transaction_id = consensus->transaction_id();
        son_consensus_sonucu_.sonuc = consensus->result();
        son_consensus_sonucu_.timeout_ile_iptal = consensus->timeout_ile_iptal_oldu();
    }

    aktif_gorev->on_exit();
    task_queue_.pop_front();
    aktif_gorev_baslatildi_ = false;

    if (gorev_iptal_edilmeli)
    {
        // Bölüm 2/3.6: oylama başarısızsa TÜM görev iptal edilir ve sürü
        // IdleTask'a döner. Bir sonraki turda kuyruk boş bulunacağı için
        // IdleTask kendiliğinden yerleşir.
        task_queue_.clear();
        return;
    }

    // Sıradaki görev varsa hemen başlat.
    if (!task_queue_.empty())
    {
        task_queue_.front()->on_enter(now);
        aktif_gorev_baslatildi_ = true;
    }
}

void SwarmManager::bekleyen_komutlari_isle(TimePoint now)
{
    Command komut;
    while (pop_command(komut))
    {
        switch (komut.type)
        {
            case CommandType::CONSENSUS:
            {
                // Kendi yayınımızı geri duymuş olabiliriz.
                if (komut.consensus.sender_id() == config_.drone_id)
                {
                    break;
                }

                // TEKLİF mi, OY mu? Teklifte vote alanı PENDING'dir
                // ("henüz oy yok"); oy mesajlarında ACK veya NACK olur.
                // Bir drone teklif aldığında kendi oyunu üretip yayınlar.
                if (komut.consensus.vote() == Vote::PENDING)
                {
                    if (config_.node_type == NodeType::DRONE)
                    {
                        teklife_oy_ver(komut.consensus);
                    }
                    break;
                }

                // Oy yalnızca AKTİF ConsensusTask'ı ilgilendirir ve
                // transaction_id'si tutmalıdır.
                ConsensusTask* aktif_oylama = nullptr;
                if (!task_queue_.empty())
                {
                    aktif_oylama = dynamic_cast<ConsensusTask*>(task_queue_.front().get());
                }

                if (aktif_oylama != nullptr &&
                    aktif_oylama->transaction_id() == komut.consensus.transaction_id())
                {
                    aktif_oylama->on_vote(komut.consensus.sender_id(), komut.consensus.vote());
                }
                break;
            }

            case CommandType::TASK_ALLOCATION:
            {
                std::unique_ptr<Task> yeni_gorev = TaskAllocationEngine::gorev_uret(
                        komut.task_allocation, config_, kendi_durumu_);

                if (yeni_gorev == nullptr)
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
                    aktif_gorev_baslatildi_ = false;
                }

                task_queue_.push_back(std::move(yeni_gorev));
                break;
            }
        }
    }

    (void)now;  // şu an kullanılmıyor; imza tutarlılığı için duruyor
}

void SwarmManager::teklife_oy_ver(const Consensus& teklif)
{
    // Karar ölçütü: göreve çıkacak kadar bataryamız var mı?
    // (Bölüm 3.6: "Her drone kendi durumunu kontrol eder.")
    const Vote karar = (kendi_durumu_.battery < KRITIK_BATARYA_YUZDESI)
            ? Vote::NACK
            : Vote::ACK;

    Consensus oy;
    oy.transaction_id(teklif.transaction_id());
    oy.sender_id(config_.drone_id);
    oy.vote(karar);
    oy.seq_num(teklif.seq_num());

    publish_consensus(oy);
}

void SwarmManager::acil_durum_gorevlerini_yerlestir(TimePoint)
{
    if (acil_durumda_)
    {
        return;  // zaten acil durum görevleri kuyrukta
    }

    acil_durumda_ = true;

    // Ne yapıyor olursak olalım bırakıp güvenli diziye geçiyoruz:
    // önce dur ve değerlendir, sonra in, sonra boşta bekle.
    task_queue_.clear();
    aktif_gorev_baslatildi_ = false;

    task_queue_.push_back(std::make_unique<FailSafeTask>(kendi_durumu_));
    task_queue_.push_back(std::make_unique<LandingTask>(kendi_durumu_));
}

// ---------------------------------------------------------------------------
//  check_emergency — her Task Engine turunda İLK çalışan kontrol
// ---------------------------------------------------------------------------

bool SwarmManager::check_emergency(TimePoint) const
{
    // 1) Kendi bataryamız kritik mi?
    if (kendi_durumu_.battery < KRITIK_BATARYA_YUZDESI)
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
    const std::lock_guard<std::mutex> kilit(peer_mutex_);
    return peer_table_.offline_peer_count() > 0;
}

// ---------------------------------------------------------------------------
//  Kendi durumunu yayınlama (Thread 1)
// ---------------------------------------------------------------------------

Heartbeat SwarmManager::build_heartbeat() const
{
    Heartbeat kalp_atisi;
    kalp_atisi.drone_id(config_.drone_id);
    kalp_atisi.node_type(config_.node_type);
    kalp_atisi.role(config_.role);

    // "Şu an ne yapıyorum" = aktif Task'ın tipi (Bölüm 3.5).
    // task_queue_ boşsa henüz bir görev başlamamıştır: INIT bildiriyoruz.
    kalp_atisi.current_task(
            task_queue_.empty() ? TaskType::INIT : task_queue_.front()->get_type());

    return kalp_atisi;
}

Telemetry SwarmManager::build_telemetry(TimePoint now)
{
    // Sayaç her yayında bir artar ve HİÇ SIFIRLANMAZ. Alıcı taraf
    // bayatlığı bununla ölçer (Bölüm 3.5).
    ++telemetri_seq_num_;

    Telemetry telemetri;
    telemetri.drone_id(config_.drone_id);
    telemetri.seq_num(telemetri_seq_num_);

    telemetri.x(kendi_durumu_.x);
    telemetri.y(kendi_durumu_.y);
    telemetri.z(kendi_durumu_.z);
    telemetri.vx(kendi_durumu_.vx);
    telemetri.vy(kendi_durumu_.vy);
    telemetri.vz(kendi_durumu_.vz);
    telemetri.battery(kendi_durumu_.battery);

    // timestamp YALNIZCA bilgi/log amaçlı (Bölüm 3.5); bayatlık kararında
    // kullanılmaz. Duvar saati (system_clock) burada uygundur, çünkü
    // amaç insan tarafından okunabilir bir zaman damgası vermek.
    const auto epoch_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch());
    telemetri.timestamp(static_cast<uint64_t>(epoch_ms.count()));

    (void)now;
    return telemetri;
}

void SwarmManager::send_self_status(TimePoint now)
{
    // Yayıncı bağlı değilse (Faz 4 öncesi veya testte) mesajı üretmenin
    // maliyetine girmiyoruz.
    if (heartbeat_yayinlayici_)
    {
        heartbeat_yayinlayici_(build_heartbeat());
    }

    if (telemetri_yayinlayici_)
    {
        telemetri_yayinlayici_(build_telemetry(now));
    }
}

void SwarmManager::set_heartbeat_publisher(HeartbeatYayinlayici yayinlayici)
{
    heartbeat_yayinlayici_ = std::move(yayinlayici);
}

void SwarmManager::set_telemetry_publisher(TelemetriYayinlayici yayinlayici)
{
    telemetri_yayinlayici_ = std::move(yayinlayici);
}

void SwarmManager::set_consensus_publisher(ConsensusYayinlayici yayinlayici)
{
    consensus_yayinlayici_ = std::move(yayinlayici);
}

void SwarmManager::set_task_allocation_publisher(GorevEmriYayinlayici yayinlayici)
{
    gorev_emri_yayinlayici_ = std::move(yayinlayici);
}

void SwarmManager::publish_consensus(const Consensus& mesaj)
{
    if (consensus_yayinlayici_)
    {
        consensus_yayinlayici_(mesaj);
    }
}

void SwarmManager::publish_task_allocation(const TaskAllocation& emir)
{
    if (gorev_emri_yayinlayici_)
    {
        gorev_emri_yayinlayici_(emir);
    }
}

std::vector<uint8_t> SwarmManager::online_drone_ids() const
{
    const std::lock_guard<std::mutex> kilit(peer_mutex_);
    return peer_table_.online_drone_ids();
}

// ---------------------------------------------------------------------------
//  Peer table güncelleme (Thread 1)
// ---------------------------------------------------------------------------

void SwarmManager::update_peer_list(TimePoint now)
{
    const std::lock_guard<std::mutex> kilit(peer_mutex_);
    peer_table_.refresh_status(now);
}

void SwarmManager::on_heartbeat_received(const Heartbeat& heartbeat, TimePoint now)
{
    // Kendi yayınımızı geri duyabiliriz (multicast); kendimizi peer
    // tablomuza eklemenin anlamı yok.
    if (heartbeat.drone_id() == config_.drone_id)
    {
        return;
    }

    const std::lock_guard<std::mutex> kilit(peer_mutex_);
    peer_table_.on_heartbeat(heartbeat, now);
}

bool SwarmManager::on_telemetry_received(const Telemetry& telemetry, TimePoint now)
{
    if (telemetry.drone_id() == config_.drone_id)
    {
        return false;
    }

    const std::lock_guard<std::mutex> kilit(peer_mutex_);
    return peer_table_.on_telemetry(telemetry, now);
}

// ---------------------------------------------------------------------------
//  Komut kuyruğu
// ---------------------------------------------------------------------------

void SwarmManager::add_command(const Command& komut)
{
    // std::lock_guard: RAII tabanlı kilit. Kurulduğu anda mutex'i kilitler,
    // kapsam (scope) bittiğinde OTOMATİK olarak açar — fonksiyondan erken
    // return edilse veya istisna atılsa bile. Elle lock()/unlock() yazmak,
    // bir yolda unlock'u unutup tüm programı kilitleme riski taşır.
    const std::lock_guard<std::mutex> kilit(command_mutex_);
    command_queue_.push_back(komut);
}

bool SwarmManager::pop_command(Command& cikti)
{
    const std::lock_guard<std::mutex> kilit(command_mutex_);

    if (command_queue_.empty())
    {
        return false;
    }

    cikti = command_queue_.front();
    command_queue_.pop_front();
    return true;
}

std::size_t SwarmManager::command_queue_size() const
{
    const std::lock_guard<std::mutex> kilit(command_mutex_);
    return command_queue_.size();
}

// ---------------------------------------------------------------------------
//  Peer table
// ---------------------------------------------------------------------------

std::size_t SwarmManager::peer_count() const
{
    const std::lock_guard<std::mutex> kilit(peer_mutex_);
    return peer_table_.peer_count();
}

std::size_t SwarmManager::online_peer_count() const
{
    const std::lock_guard<std::mutex> kilit(peer_mutex_);
    return peer_table_.online_peer_count();
}

// ---------------------------------------------------------------------------
//  Görev kuyruğu  (kilit yok: yalnızca Task Engine thread'i dokunur)
// ---------------------------------------------------------------------------

void SwarmManager::push_task(std::unique_ptr<Task> gorev)
{
    // std::move: unique_ptr KOPYALANAMAZ (tek sahiplik kuralı), yalnızca
    // TAŞINABİLİR. std::move, "bu nesnenin sahipliğini devrediyorum" demenin
    // yoludur; taşımadan sonra çağıranın elindeki işaretçi boşalır.
    task_queue_.push_back(std::move(gorev));
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
