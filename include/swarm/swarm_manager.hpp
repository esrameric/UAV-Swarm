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

#include <cstddef>
#include <deque>
#include <memory>
#include <mutex>

#include "swarm/command.hpp"
#include "swarm/peer_manager.hpp"
#include "swarm/task/task.hpp"

namespace swarm {

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
};

}  // namespace swarm
