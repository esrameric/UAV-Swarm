#include "swarm/swarm_manager.hpp"

#include <utility>

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
//  Thread gövdeleri — gerçek işleri Faz 3.4'te dolduruluyor
// ---------------------------------------------------------------------------

void SwarmManager::drone_list_dongusu()
{
    while (calisiyor_)
    {
        std::this_thread::sleep_for(HEARTBEAT_PERIYODU);
    }
}

void SwarmManager::komut_dongusu()
{
    while (calisiyor_)
    {
        std::this_thread::sleep_for(KOMUT_PERIYODU);
    }
}

void SwarmManager::komut_yurutme_dongusu()
{
    while (calisiyor_)
    {
        std::this_thread::sleep_for(TASK_ENGINE_PERIYODU);
    }
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
