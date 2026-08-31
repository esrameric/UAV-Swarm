// ============================================================================
//  Basit log yardımcısı
//
//  Neden var: Faz 6'daki entegrasyon testleri container'ların log çıktısını
//  okuyup doğruluyor. Bunun çalışması için satırların (a) öngörülebilir bir
//  biçimde olması, (b) ANINDA yazılması gerekiyor — buffer içinde bekleyen
//  bir satır, container durdurulduğunda kaybolabilir.
//
//  Biçim:  [<etiket>] <mesaj>
//  Örnek:  [peer] yeni peer: id=2 role=STRIKER
// ============================================================================

#pragma once

#include <iostream>
#include <mutex>
#include <string>

namespace swarm {

// Birden fazla thread aynı anda yazarsa satırlar birbirine karışmasın diye
// tek bir lock kullanıyoruz.
inline std::mutex& log_lock()
{
    // Fonksiyon içi static: ilk kullanımda bir kez kurulur (Meyers kalıbı).
    static std::mutex lock;
    return lock;
}

inline void log(const std::string& tag, const std::string& message)
{
    const std::lock_guard<std::mutex> lock(log_lock());

    // std::endl yalnızca satır sonu koymaz, buffer'ı da BOŞALTIR (flush).
    // Docker log'unun anlık görünmesi için burada tam olarak bunu istiyoruz.
    std::cout << "[" << tag << "] " << message << std::endl;
}

}  // namespace swarm
