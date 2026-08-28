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

#pragma once

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

private:
    // Kurucu ve yıkıcı `private`: yalnızca sınıfın kendisi (get_instance)
    // örnek oluşturabilir/yok edebilir.
    SwarmManager() = default;
    ~SwarmManager() = default;
};

}  // namespace swarm
