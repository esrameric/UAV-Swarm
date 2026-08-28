// ============================================================================
//  Basit log yardımcısı
//
//  Neden var: Faz 6'daki entegrasyon testleri container'ların log çıktısını
//  okuyup doğruluyor. Bunun çalışması için satırların (a) öngörülebilir bir
//  biçimde olması, (b) ANINDA yazılması gerekiyor — tampon içinde bekleyen
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
// tek bir kilit kullanıyoruz.
inline std::mutex& log_kilidi()
{
    // Fonksiyon içi static: ilk kullanımda bir kez kurulur (Meyers kalıbı).
    static std::mutex kilit;
    return kilit;
}

inline void log(const std::string& etiket, const std::string& mesaj)
{
    const std::lock_guard<std::mutex> koruma(log_kilidi());

    // std::endl yalnızca satır sonu koymaz, tamponu da BOŞALTIR (flush).
    // Docker log'unun anlık görünmesi için burada tam olarak bunu istiyoruz.
    std::cout << "[" << etiket << "] " << mesaj << std::endl;
}

}  // namespace swarm
