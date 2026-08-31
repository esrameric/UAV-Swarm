// ============================================================================
//  Task — tüm görevlerin ortak SOYUT ARAYÜZÜ (State Pattern)
//
//  Klasik bir if/else state machine yerine polimorfik bir Task hiyerarşisi
//  kullanıyoruz (Bölüm 2). Kazancı şu: yeni bir görev eklemek için mevcut
//  hiçbir dosyaya dokunmak gerekmez — yalnızca yeni bir Task child'ı yazılır.
//  Buna Open/Closed ilkesi denir: "genişletmeye açık, değiştirmeye kapalı".
//
//  Task Engine (Faz 3) her döngü turunda aktif task üzerinde şunu yapar:
//
//      aktif_task->run(simdi);
//      if (aktif_task->is_finished()) {
//          aktif_task->on_exit();
//          <sıradaki task>->on_enter(simdi);
//      }
//
//  Yaşam döngüsü:  on_enter()  ->  run() run() run() ...  ->  on_exit()
// ============================================================================

#pragma once

#include <chrono>

#include "SwarmEnums.hpp"

namespace swarm {

// Proje boyunca "bir an" demek için kullanılan tip. steady_clock monotoniktir
// (geri gitmez), süre ölçmenin doğru aracıdır.
using TimePoint = std::chrono::steady_clock::time_point;

// SOYUT SINIF (abstract class): doğrudan örneği oluşturulamayan, yalnızca
// miras alınmak için var olan sınıf. Bir sınıfı soyut yapan şey, en az bir
// "saf sanal" (pure virtual) fonksiyona sahip olmasıdır: `= 0` ile biten
// bildirimler. Bu fonksiyonların gövdesi burada yoktur; her child task
// kendi gövdesini yazmak ZORUNDADIR.
class Task
{
public:
    // SANAL YIKICI (virtual destructor) — burada olması hayati.
    // Task Engine, task'ları `Task*` (taban sınıf işaretçisi) üzerinden
    // tutup siler. Yıkıcı `virtual` olmasaydı, silme anında yalnızca
    // Task'ın yıkıcısı çalışır, child'ın yıkıcısı ATLANIRDI — sessiz bir
    // memory leak. Kural: polimorfik olarak kullanılan her taban sınıfın
    // yıkıcısı virtual olmalıdır.
    //
    // `= default`: "derleyici bunun varsayılan gövdesini kendi yazsın".
    virtual ~Task() = default;

    // Task queue'nun başına geçtiğinde BİR KEZ çağrılır.
    // Hazırlık işleri (başlangıç zamanını kaydetme, hedefi hesaplama)
    // buraya yazılır.
    virtual void on_enter(TimePoint now) = 0;

    // Task aktif olduğu sürece her döngü turunda çağrılır.
    // Bloklamamalıdır — içeride uyumaz, bekleme yapmaz; işini küçük
    // adımlarla ilerletir.
    virtual void run(TimePoint now) = 0;

    // Task queue'dan çıkarken BİR KEZ çağrılır. Temizlik işleri buraya.
    virtual void on_exit() = 0;

    // "İşim bitti mi?" Task Engine bunu her turda sorar; true dönerse
    // sıradaki task'a geçilir.
    virtual bool is_finished() const = 0;

    // Bu task'ın tipi. Heartbeat mesajında "şu an ne yapıyorum" bilgisi
    // olarak yayınlanır (Bölüm 3.4).
    virtual TaskType get_type() const = 0;
};

}  // namespace swarm
