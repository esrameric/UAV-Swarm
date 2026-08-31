// ============================================================================
//  PeerInfo — sürüdeki BAŞKA bir düğüm hakkında RAM'de tuttuğumuz bilgi
//
//  Her düğüm, duyduğu diğer düğümler için birer PeerInfo kaydı tutar
//  (peer table). Bu kayıt diske yazılmaz, ağda gönderilmez — tamamen yerel
//  bir görüntüdür: "ben şu an sürüyü böyle görüyorum".
//
//  Bölüm 3.2'deki son hâliyle kodlanmıştır.
// ============================================================================

// `#pragma once`: bu başlık dosyası aynı derleme biriminde birden fazla kez
// #include edilse bile içeriği yalnızca bir kez işlensin. Aynı tipin iki kez
// tanımlanmasından kaynaklanan derleme hatalarını önler.
#pragma once

#include <chrono>
#include <cstdint>

// IDL'den üretilen enum'lar (NodeType, DroneRole, TaskType).
#include "SwarmEnums.hpp"

namespace swarm {

// `struct` ile `class` C++'ta neredeyse aynı şeydir; tek fark varsayılan
// erişim seviyesidir (struct'ta public, class'ta private). Gelenek olarak
// "sadece veri taşıyan, davranışı olmayan" tipler için struct kullanılır —
// PeerInfo tam olarak böyle bir tiptir.
struct PeerInfo
{
    // --- Kimlik --------------------------------------------------------------

    // Düğümün kimliği. GCS = 0, drone'lar 1..3 (Bölüm 4).
    // `= 0` yazımı "default member initializer"dır: bu alan, kurucu metotta
    // ayrıca belirtilmezse otomatik olarak bu değeri alır. Böylece
    // "uninitialized" bir değer okuma hatası baştan imkânsız hâle gelir.
    uint8_t drone_id = 0;

    // Bu düğüm bir drone mu, GCS mi?
    NodeType node_type = NodeType::DRONE;

    // Yalnızca node_type == DRONE iken anlamlıdır (Bölüm 3.5).
    DroneRole role = DroneRole::SCOUT;

    // --- Sürü üyeliği --------------------------------------------------------

    // 0 = hiçbir sürüde değil, 1+ = geçerli sürü kimliği.
    //
    // NEDEN SENTİNEL 0, -1 DEĞİL: swarm_id işaretsiz (uint8_t) bir tiptir.
    // Buna -1 atanırsa değer sessizce 255'e döner ve sonraki `== -1`
    // karşılaştırmaları C++'ın integer promotion kuralları yüzünden HER ZAMAN
    // false verir. Bu, bilinen bir unsigned-tip tuzağıdır. Sürü kimlikleri
    // zaten 1'den başladığı için 0 doğal ve güvenli sentinel değerdir.
    uint8_t swarm_id = 0;

    // --- Durum ---------------------------------------------------------------

    // Heartbeat'ten gelen "şu an ne yapıyor" bilgisi.
    TaskType current_task = TaskType::INIT;

    // Bu peer'dan EN SON heartbeat aldığımız an — ALICININ (yani bizim)
    // kendi saatimize göre. Gönderenin mesaja gömdüğü bir zaman damgası
    // DEĞİLDİR (Bölüm 3.5).
    //
    // std::chrono::steady_clock: geriye gitmeyen, dışarıdan ayarlanamayan
    // (NTP düzeltmesi, yaz saati vb. etkilemez) monotonik saat. "Ne kadar
    // süre geçti?" sorusu için doğru araç budur; duvar saati (system_clock)
    // değil.
    std::chrono::steady_clock::time_point last_heartbeat_local{};

    // Bu peer'dan görülen en yüksek telemetri sıra numarası. Bayat/sırası
    // bozulmuş paketleri elemek için kullanılır (Bölüm 3.5).
    uint32_t last_seen_seq = 0;

    // --- Yardımcılar ---------------------------------------------------------

    // Bu peer herhangi bir sürünün üyesi mi?
    //
    // Ayrı bir `is_in_swarm` alanı BİLİNÇLİ OLARAK YOK: swarm_id'nin kendisi
    // bu bilgiyi zaten taşıyor. İki ayrı alan tutmak, ikisinin birbiriyle
    // çelişme ihtimalini doğururdu.
    //
    // Sondaki `const`: "bu fonksiyon nesneyi değiştirmez" sözüdür. Derleyici
    // bu sözü zorlar; ayrıca const bir PeerInfo üzerinde de çağrılabilmesini
    // sağlar.
    bool is_in_swarm() const
    {
        return swarm_id != 0;
    }
};

}  // namespace swarm
