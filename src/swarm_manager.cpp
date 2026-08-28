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
//  Yardımcı fonksiyonlar — gerçek gövdeleri Faz 3.5'te
// ---------------------------------------------------------------------------

bool SwarmManager::check_emergency(TimePoint) const
{
    // Faz 3.5'te doldurulacak (kaybolan peer, kritik batarya).
    // Şimdilik acil durum yok kabul ediliyor.
    return false;
}

void SwarmManager::send_self_status(TimePoint)
{
    // Faz 3.5'te doldurulacak: kendi Heartbeat'ini yayınlar.
}

void SwarmManager::update_peer_list(TimePoint)
{
    // Faz 3.5'te doldurulacak: gelen heartbeat'lerle peer table'ı günceller.
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
