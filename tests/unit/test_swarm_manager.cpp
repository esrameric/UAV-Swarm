// ============================================================================
//  Faz 3.1 — SwarmManager Singleton garantisinin testi
// ============================================================================

#include <gtest/gtest.h>

#include "swarm/swarm_manager.hpp"

#include <thread>
#include <type_traits>
#include <vector>

TEST(SwarmManagerSingleton, HerCagriAyniOrnegiDondurur)
{
    // İki ayrı çağrının AYNI nesneyi verdiğini adreslerini karşılaştırarak
    // doğruluyoruz. `&` bir nesnenin bellekteki adresini verir.
    swarm::SwarmManager& birinci = swarm::SwarmManager::get_instance();
    swarm::SwarmManager& ikinci = swarm::SwarmManager::get_instance();

    EXPECT_EQ(&birinci, &ikinci);
}

TEST(SwarmManagerSingleton, KopyalanamazVeTasinamaz)
{
    // std::is_copy_constructible<T>::value, "T kopyalanabilir mi?" sorusunu
    // DERLEME ZAMANINDA cevaplar. Kopyalama silindiği için false olmalı.
    // Bu, singleton garantisinin kazara kırılmasına karşı koruma.
    static_assert(!std::is_copy_constructible<swarm::SwarmManager>::value,
                  "SwarmManager kopyalanabilir olmamali");
    static_assert(!std::is_copy_assignable<swarm::SwarmManager>::value,
                  "SwarmManager kopya atamasi kabul etmemeli");
    static_assert(!std::is_move_constructible<swarm::SwarmManager>::value,
                  "SwarmManager tasinabilir olmamali");

    EXPECT_FALSE(std::is_copy_constructible<swarm::SwarmManager>::value);
}

TEST(SwarmManagerSingleton, DisaridanOrnekOlusturulamaz)
{
    // Kurucu private olduğu için `swarm::SwarmManager m;` yazımı derlenmez.
    // Bunu derleme zamanında şöyle doğruluyoruz:
    static_assert(!std::is_default_constructible<swarm::SwarmManager>::value,
                  "SwarmManager disaridan kurulabilir olmamali");

    EXPECT_FALSE(std::is_default_constructible<swarm::SwarmManager>::value);
}

TEST(SwarmManagerSingleton, EszamanliErisimdeDeTekOrnek)
{
    // Meyers Singleton'ın asıl vaadi: birden fazla thread aynı anda
    // get_instance() çağırsa bile nesne yalnızca BİR KEZ kurulur ve
    // hepsi aynı adresi görür.
    //
    // std::thread: işletim sisteminden yeni bir çalışma iş parçacığı ister.
    constexpr int THREAD_SAYISI = 8;
    std::vector<swarm::SwarmManager*> adresler(THREAD_SAYISI, nullptr);
    std::vector<std::thread> threadler;

    for (int sira = 0; sira < THREAD_SAYISI; ++sira)
    {
        // LAMBDA: yerinde tanımlanan isimsiz fonksiyon. Köşeli parantez
        // içindeki `&adresler, sira` "yakalama listesi"dir: lambda'nın
        // dışarıdaki hangi değişkenleri kullanacağını söyler. `&` referansla
        // (asıl nesne), isim tek başına ise kopyayla yakalar.
        threadler.emplace_back([&adresler, sira]() {
            adresler[static_cast<std::size_t>(sira)] = &swarm::SwarmManager::get_instance();
        });
    }

    // join(): thread'in bitmesini bekler. Beklemezsek main thread önce
    // bitip programı sonlandırabilir.
    for (std::thread& thread : threadler)
    {
        thread.join();
    }

    for (const swarm::SwarmManager* adres : adresler)
    {
        EXPECT_EQ(adres, &swarm::SwarmManager::get_instance());
    }
}
