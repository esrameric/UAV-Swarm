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

// ============================================================================
//  Faz 3.2 — mutex korumalı üyeler
//
//  DİKKAT: SwarmManager bir Singleton olduğu için testler AYNI nesneyi
//  paylaşır. Bu yüzden her test, işe başlamadan önce kuyrukları boşaltır;
//  böylece testlerin çalışma sırası sonucu etkilemez.
// ============================================================================

#include "swarm/command.hpp"
#include "swarm/task/idle_task.hpp"
#include "swarm/task/init_task.hpp"

namespace {

void kuyruklari_bosalt(swarm::SwarmManager& yonetici)
{
    swarm::Command atilacak;
    while (yonetici.pop_command(atilacak))
    {
    }
    yonetici.clear_task_queue();
}

swarm::TaskAllocation gorev_emri_olustur(uint32_t task_id, swarm::DroneRole rol)
{
    swarm::TaskAllocation emir;
    emir.task_id(task_id);
    emir.target_role(rol);
    emir.target_x(10.0);
    emir.target_y(20.0);
    return emir;
}

}  // namespace

TEST(SwarmManagerKuyruk, BaslangictaBosaltilabiliyor)
{
    swarm::SwarmManager& yonetici = swarm::SwarmManager::get_instance();
    kuyruklari_bosalt(yonetici);

    EXPECT_EQ(yonetici.command_queue_size(), 0u);
    EXPECT_EQ(yonetici.task_queue_size(), 0u);
    EXPECT_EQ(yonetici.current_task(), nullptr);
}

TEST(SwarmManagerKuyruk, KomutlarFifoSirasiylaIslenir)
{
    swarm::SwarmManager& yonetici = swarm::SwarmManager::get_instance();
    kuyruklari_bosalt(yonetici);

    yonetici.add_command(swarm::Command::gorev_emri(
            gorev_emri_olustur(1, swarm::DroneRole::SCOUT)));
    yonetici.add_command(swarm::Command::gorev_emri(
            gorev_emri_olustur(2, swarm::DroneRole::STRIKER)));

    EXPECT_EQ(yonetici.command_queue_size(), 2u);

    swarm::Command birinci;
    ASSERT_TRUE(yonetici.pop_command(birinci));
    EXPECT_EQ(birinci.type, swarm::CommandType::TASK_ALLOCATION);
    EXPECT_EQ(birinci.task_allocation.task_id(), 1u);

    swarm::Command ikinci;
    ASSERT_TRUE(yonetici.pop_command(ikinci));
    EXPECT_EQ(ikinci.task_allocation.task_id(), 2u);

    EXPECT_EQ(yonetici.command_queue_size(), 0u);
}

TEST(SwarmManagerKuyruk, BosKuyruktanPopFalseDoner)
{
    swarm::SwarmManager& yonetici = swarm::SwarmManager::get_instance();
    kuyruklari_bosalt(yonetici);

    swarm::Command cikti;
    EXPECT_FALSE(yonetici.pop_command(cikti));
}

TEST(SwarmManagerKuyruk, ConsensusOyuKomutOlarakTasinabilir)
{
    swarm::SwarmManager& yonetici = swarm::SwarmManager::get_instance();
    kuyruklari_bosalt(yonetici);

    swarm::Consensus oy;
    oy.transaction_id(42);
    oy.sender_id(3);
    oy.vote(swarm::Vote::ACK);

    yonetici.add_command(swarm::Command::consensus_oyu(oy));

    swarm::Command alinan;
    ASSERT_TRUE(yonetici.pop_command(alinan));
    EXPECT_EQ(alinan.type, swarm::CommandType::CONSENSUS);
    EXPECT_EQ(alinan.consensus.transaction_id(), 42u);
    EXPECT_EQ(alinan.consensus.vote(), swarm::Vote::ACK);
}

TEST(SwarmManagerKuyruk, GorevKuyruguSirayiKorur)
{
    swarm::SwarmManager& yonetici = swarm::SwarmManager::get_instance();
    kuyruklari_bosalt(yonetici);

    swarm::DroneState durum;
    yonetici.push_task(std::make_unique<swarm::InitTask>());
    yonetici.push_task(std::make_unique<swarm::IdleTask>(durum));

    EXPECT_EQ(yonetici.task_queue_size(), 2u);

    // Aktif görev kuyruğun BAŞIDIR.
    ASSERT_NE(yonetici.current_task(), nullptr);
    EXPECT_EQ(yonetici.current_task()->get_type(), swarm::TaskType::INIT);
}

TEST(SwarmManagerKuyruk, ClearTaskQueueTumGorevleriIptalEder)
{
    // Consensus ABORTED olduğunda sürünün IdleTask'a dönebilmesi için
    // kuyruğun boşaltılabilmesi gerekir (Bölüm 2/3.6).
    swarm::SwarmManager& yonetici = swarm::SwarmManager::get_instance();
    kuyruklari_bosalt(yonetici);

    swarm::DroneState durum;
    yonetici.push_task(std::make_unique<swarm::InitTask>());
    yonetici.push_task(std::make_unique<swarm::IdleTask>(durum));
    ASSERT_EQ(yonetici.task_queue_size(), 2u);

    yonetici.clear_task_queue();

    EXPECT_EQ(yonetici.task_queue_size(), 0u);
    EXPECT_EQ(yonetici.current_task(), nullptr);
}

TEST(SwarmManagerKuyruk, PeerTablosuBaslangictaBos)
{
    const swarm::SwarmManager& yonetici = swarm::SwarmManager::get_instance();

    // Bu test henüz peer eklenmemişken çalışır; peer ekleme Faz 3.5'te
    // update_peer_list() ile gelecek.
    EXPECT_EQ(yonetici.online_peer_count(), 0u);
}

TEST(SwarmManagerKuyruk, EszamanliKomutEklemeVeriYarisiYaratmaz)
{
    // MUTEX'İN ASIL SINANDIĞI TEST: 4 thread aynı anda komut ekliyor.
    // Kilit olmasaydı deque'nun iç yapısı bozulur, sayı tutmaz veya
    // program çökerdi.
    swarm::SwarmManager& yonetici = swarm::SwarmManager::get_instance();
    kuyruklari_bosalt(yonetici);

    constexpr int THREAD_SAYISI = 4;
    constexpr int KOMUT_SAYISI = 250;

    std::vector<std::thread> threadler;
    for (int thread_no = 0; thread_no < THREAD_SAYISI; ++thread_no)
    {
        threadler.emplace_back([&yonetici, thread_no]() {
            for (int sira = 0; sira < KOMUT_SAYISI; ++sira)
            {
                yonetici.add_command(swarm::Command::gorev_emri(
                        gorev_emri_olustur(
                                static_cast<uint32_t>(thread_no * 1000 + sira),
                                swarm::DroneRole::SCOUT)));
            }
        });
    }

    for (std::thread& thread : threadler)
    {
        thread.join();
    }

    // Hiçbir komut kaybolmamalı, fazladan da olmamalı.
    EXPECT_EQ(yonetici.command_queue_size(),
              static_cast<std::size_t>(THREAD_SAYISI * KOMUT_SAYISI));

    kuyruklari_bosalt(yonetici);
}
