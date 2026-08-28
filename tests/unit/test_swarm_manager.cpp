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
#include "swarm/task/consensus_task.hpp"
#include "swarm/task/hover_task.hpp"
#include "swarm/task/idle_task.hpp"
#include "swarm/task/scout_search_task.hpp"
#include "swarm/task/init_task.hpp"

namespace {

using namespace std::chrono_literals;

// Testlerin sabit zaman referansı (bkz. test_tasks.cpp'deki aynı kalıp).
const swarm::TimePoint BASLANGIC{};

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

// ============================================================================
//  Faz 3.3 — init() ve run()
// ============================================================================

TEST(SwarmManagerYasamDongusu, InitKimligiAyarlar)
{
    swarm::SwarmManager& yonetici = swarm::SwarmManager::get_instance();

    swarm::SwarmConfig config;
    config.drone_id = 2;
    config.node_type = swarm::NodeType::DRONE;
    config.role = swarm::DroneRole::STRIKER;
    config.domain_id = 42;

    yonetici.init(config);

    EXPECT_EQ(yonetici.config().drone_id, 2u);
    EXPECT_EQ(yonetici.config().node_type, swarm::NodeType::DRONE);
    EXPECT_EQ(yonetici.config().role, swarm::DroneRole::STRIKER);
    EXPECT_EQ(yonetici.config().domain_id, 42u);
}

TEST(SwarmManagerYasamDongusu, InitKuyruklariVeDurumuSifirlar)
{
    swarm::SwarmManager& yonetici = swarm::SwarmManager::get_instance();

    // Önce kirletiyoruz...
    yonetici.add_command(swarm::Command::gorev_emri(
            gorev_emri_olustur(1, swarm::DroneRole::SCOUT)));
    yonetici.push_task(std::make_unique<swarm::InitTask>());
    yonetici.drone_state().x = 123.0;
    ASSERT_GT(yonetici.command_queue_size(), 0u);

    // ...init temizlemeli.
    yonetici.init(swarm::SwarmConfig{});

    EXPECT_EQ(yonetici.command_queue_size(), 0u);
    EXPECT_EQ(yonetici.task_queue_size(), 0u);
    EXPECT_EQ(yonetici.peer_count(), 0u);
    EXPECT_DOUBLE_EQ(yonetici.drone_state().x, 0.0);
}

TEST(SwarmManagerYasamDongusu, GcsKimligiKurulabilir)
{
    swarm::SwarmManager& yonetici = swarm::SwarmManager::get_instance();

    swarm::SwarmConfig config;
    config.drone_id = 0;
    config.node_type = swarm::NodeType::GCS;

    yonetici.init(config);

    EXPECT_EQ(yonetici.config().node_type, swarm::NodeType::GCS);
    EXPECT_EQ(yonetici.config().drone_id, 0u);
}

TEST(SwarmManagerYasamDongusu, RunThreadleriBaslatirStopBekler)
{
    swarm::SwarmManager& yonetici = swarm::SwarmManager::get_instance();
    yonetici.init(swarm::SwarmConfig{});

    EXPECT_FALSE(yonetici.is_running());

    yonetici.run();
    EXPECT_TRUE(yonetici.is_running());

    // stop() thread'lerin bitmesini BEKLER (join). Dönüş sonrası hiçbir
    // thread çalışmıyor olmalı.
    yonetici.stop();
    EXPECT_FALSE(yonetici.is_running());
}

TEST(SwarmManagerYasamDongusu, IkinciRunCagrisiEtkisiz)
{
    swarm::SwarmManager& yonetici = swarm::SwarmManager::get_instance();
    yonetici.init(swarm::SwarmConfig{});

    yonetici.run();
    yonetici.run();  // ikinci çağrı yeni thread AÇMAMALI
    EXPECT_TRUE(yonetici.is_running());

    yonetici.stop();
    EXPECT_FALSE(yonetici.is_running());
}

TEST(SwarmManagerYasamDongusu, CalismayanDugumdeStopGuvenli)
{
    swarm::SwarmManager& yonetici = swarm::SwarmManager::get_instance();
    yonetici.init(swarm::SwarmConfig{});

    // Hiç run() çağrılmadan stop() çağırmak çökmemelidir.
    yonetici.stop();
    yonetici.stop();

    EXPECT_FALSE(yonetici.is_running());
}

TEST(SwarmManagerYasamDongusu, BasDurBasDurDongusuCalisir)
{
    // Thread'ler düzgün join edildiği için düğüm tekrar tekrar
    // başlatılıp durdurulabilmeli.
    swarm::SwarmManager& yonetici = swarm::SwarmManager::get_instance();
    yonetici.init(swarm::SwarmConfig{});

    for (int tur = 0; tur < 3; ++tur)
    {
        yonetici.run();
        ASSERT_TRUE(yonetici.is_running());
        yonetici.stop();
        ASSERT_FALSE(yonetici.is_running());
    }
}

// ============================================================================
//  Faz 3.4 — Task Engine (Thread 3) davranışı
//
//  Thread başlatmadan test ediyoruz: task_engine_adimi(now) tek bir turdur,
//  thread yalnızca onu döngüde çağırır. Zamanı biz verdiğimiz için
//  senaryolar deterministik.
// ============================================================================

#include "swarm/task_allocation_engine.hpp"

namespace {

swarm::SwarmManager& drone_olarak_hazirla(uint8_t drone_id, swarm::DroneRole rol)
{
    swarm::SwarmManager& yonetici = swarm::SwarmManager::get_instance();

    swarm::SwarmConfig config;
    config.drone_id = drone_id;
    config.node_type = swarm::NodeType::DRONE;
    config.role = rol;

    yonetici.init(config);
    return yonetici;
}

}  // namespace

TEST(TaskEngine, BosKuyrukIdleTaskaDuser)
{
    swarm::SwarmManager& yonetici = drone_olarak_hazirla(1, swarm::DroneRole::SCOUT);
    ASSERT_EQ(yonetici.task_queue_size(), 0u);

    yonetici.task_engine_adimi(BASLANGIC);

    ASSERT_NE(yonetici.current_task(), nullptr);
    EXPECT_EQ(yonetici.current_task()->get_type(), swarm::TaskType::IDLE);
}

TEST(TaskEngine, BitenGorevKuyruktanCikarilirVeSiradakiBaslar)
{
    swarm::SwarmManager& yonetici = drone_olarak_hazirla(1, swarm::DroneRole::SCOUT);

    // InitTask ilk run()'da biter; ardindan HoverTask aktif olmali.
    yonetici.push_task(std::make_unique<swarm::InitTask>());
    yonetici.push_task(std::make_unique<swarm::HoverTask>(yonetici.drone_state(), 1000ms));
    ASSERT_EQ(yonetici.task_queue_size(), 2u);

    yonetici.task_engine_adimi(BASLANGIC);

    EXPECT_EQ(yonetici.task_queue_size(), 1u);
    ASSERT_NE(yonetici.current_task(), nullptr);
    EXPECT_EQ(yonetici.current_task()->get_type(), swarm::TaskType::HOVER);
}

TEST(TaskEngine, KuyrukTukeninceIdleTaskaDonulur)
{
    swarm::SwarmManager& yonetici = drone_olarak_hazirla(1, swarm::DroneRole::SCOUT);
    yonetici.push_task(std::make_unique<swarm::InitTask>());

    yonetici.task_engine_adimi(BASLANGIC);          // InitTask biter
    yonetici.task_engine_adimi(BASLANGIC + 20ms);   // kuyruk bos -> IdleTask

    ASSERT_NE(yonetici.current_task(), nullptr);
    EXPECT_EQ(yonetici.current_task()->get_type(), swarm::TaskType::IDLE);
}

// --- Gorev dagitimi (TaskAllocationEngine) ---------------------------------

TEST(TaskEngine, ScoutRoluAramaGoreviAlir)
{
    swarm::SwarmManager& yonetici = drone_olarak_hazirla(1, swarm::DroneRole::SCOUT);

    yonetici.add_command(swarm::Command::gorev_emri(
            gorev_emri_olustur(10, swarm::DroneRole::SCOUT)));

    yonetici.task_engine_adimi(BASLANGIC);

    ASSERT_NE(yonetici.current_task(), nullptr);
    EXPECT_EQ(yonetici.current_task()->get_type(), swarm::TaskType::SCOUT_SEARCH);
}

TEST(TaskEngine, StrikerRoluHedefeGidisGoreviAlir)
{
    swarm::SwarmManager& yonetici = drone_olarak_hazirla(2, swarm::DroneRole::STRIKER);

    yonetici.add_command(swarm::Command::gorev_emri(
            gorev_emri_olustur(11, swarm::DroneRole::STRIKER)));

    yonetici.task_engine_adimi(BASLANGIC);

    ASSERT_NE(yonetici.current_task(), nullptr);
    EXPECT_EQ(yonetici.current_task()->get_type(), swarm::TaskType::GO_TO_TARGET);
}

TEST(TaskEngine, BaskaRoleGonderilenEmirYokSayilir)
{
    // Heterojen sure: emir bir DRONE'a degil bir ROLE gonderilir.
    swarm::SwarmManager& yonetici = drone_olarak_hazirla(1, swarm::DroneRole::SCOUT);

    yonetici.add_command(swarm::Command::gorev_emri(
            gorev_emri_olustur(12, swarm::DroneRole::STRIKER)));

    yonetici.task_engine_adimi(BASLANGIC);

    // Emir yok sayildi -> IdleTask'ta kaldik.
    ASSERT_NE(yonetici.current_task(), nullptr);
    EXPECT_EQ(yonetici.current_task()->get_type(), swarm::TaskType::IDLE);
}

TEST(TaskEngine, GcsUcusGoreviAlmaz)
{
    swarm::SwarmManager& yonetici = swarm::SwarmManager::get_instance();
    swarm::SwarmConfig config;
    config.drone_id = 0;
    config.node_type = swarm::NodeType::GCS;
    yonetici.init(config);

    yonetici.add_command(swarm::Command::gorev_emri(
            gorev_emri_olustur(13, swarm::DroneRole::SCOUT)));

    yonetici.task_engine_adimi(BASLANGIC);

    ASSERT_NE(yonetici.current_task(), nullptr);
    EXPECT_EQ(yonetici.current_task()->get_type(), swarm::TaskType::IDLE);
}

TEST(TaskEngine, YeniGorevEmriIdleTaskiYerindenEder)
{
    swarm::SwarmManager& yonetici = drone_olarak_hazirla(1, swarm::DroneRole::SCOUT);

    yonetici.task_engine_adimi(BASLANGIC);
    ASSERT_EQ(yonetici.current_task()->get_type(), swarm::TaskType::IDLE);

    yonetici.add_command(swarm::Command::gorev_emri(
            gorev_emri_olustur(14, swarm::DroneRole::SCOUT)));
    yonetici.task_engine_adimi(BASLANGIC + 20ms);

    // Bosta beklemeye devam etmenin anlami yok: yeni goreve geciliyor.
    EXPECT_EQ(yonetici.current_task()->get_type(), swarm::TaskType::SCOUT_SEARCH);
    EXPECT_EQ(yonetici.task_queue_size(), 1u);
}

// --- Consensus oylarinin yonlendirilmesi -----------------------------------

TEST(TaskEngine, ConsensusOyuAktifOylamayaIletilir)
{
    swarm::SwarmManager& yonetici = drone_olarak_hazirla(1, swarm::DroneRole::SCOUT);

    auto oylama = std::make_unique<swarm::ConsensusTask>(
            77, std::vector<uint8_t>{2, 3}, 5000ms);
    swarm::ConsensusTask* oylama_ptr = oylama.get();
    yonetici.push_task(std::move(oylama));

    swarm::Consensus oy;
    oy.transaction_id(77);
    oy.sender_id(2);
    oy.vote(swarm::Vote::ACK);
    yonetici.add_command(swarm::Command::consensus_oyu(oy));

    yonetici.task_engine_adimi(BASLANGIC);

    EXPECT_EQ(oylama_ptr->oy_durumu(2), swarm::Vote::ACK);
}

TEST(TaskEngine, YanlisTransactionIdliOyYokSayilir)
{
    swarm::SwarmManager& yonetici = drone_olarak_hazirla(1, swarm::DroneRole::SCOUT);

    auto oylama = std::make_unique<swarm::ConsensusTask>(
            77, std::vector<uint8_t>{2}, 5000ms);
    swarm::ConsensusTask* oylama_ptr = oylama.get();
    yonetici.push_task(std::move(oylama));

    swarm::Consensus oy;
    oy.transaction_id(999);   // baska bir oylama turuna ait
    oy.sender_id(2);
    oy.vote(swarm::Vote::ACK);
    yonetici.add_command(swarm::Command::consensus_oyu(oy));

    yonetici.task_engine_adimi(BASLANGIC);

    EXPECT_EQ(oylama_ptr->oy_durumu(2), swarm::Vote::PENDING);
}

TEST(TaskEngine, ConsensusIptalindeTumGorevKuyruguBosaltilir)
{
    // Bolum 2/3.6: oylama basarisizsa yalnizca o task bitmez, TUM gorev
    // iptal edilir ve suru IdleTask'a doner.
    swarm::SwarmManager& yonetici = drone_olarak_hazirla(1, swarm::DroneRole::SCOUT);

    yonetici.push_task(std::make_unique<swarm::ConsensusTask>(
            88, std::vector<uint8_t>{2, 3}, 5000ms));
    // Oylama gecerse yapilacak gorevler de kuyrukta bekliyor:
    yonetici.push_task(std::make_unique<swarm::ScoutSearchTask>(
            yonetici.drone_state(), 100.0, 100.0));
    yonetici.push_task(std::make_unique<swarm::HoverTask>(
            yonetici.drone_state(), 1000ms));
    ASSERT_EQ(yonetici.task_queue_size(), 3u);

    // Oylama baslar ama kimse cevap vermez; 5 saniye sonra timeout.
    yonetici.task_engine_adimi(BASLANGIC);
    ASSERT_EQ(yonetici.task_queue_size(), 3u);

    yonetici.task_engine_adimi(BASLANGIC + 5s);

    // Bekleyen TUM gorevler iptal edildi.
    EXPECT_EQ(yonetici.task_queue_size(), 0u);

    // Bir sonraki turda IdleTask'a dusuluyor.
    yonetici.task_engine_adimi(BASLANGIC + 5s + 20ms);
    ASSERT_NE(yonetici.current_task(), nullptr);
    EXPECT_EQ(yonetici.current_task()->get_type(), swarm::TaskType::IDLE);
}

TEST(TaskEngine, BasariliConsensusSonrasiSiradakiGoreveGecilir)
{
    swarm::SwarmManager& yonetici = drone_olarak_hazirla(1, swarm::DroneRole::SCOUT);

    yonetici.push_task(std::make_unique<swarm::ConsensusTask>(
            99, std::vector<uint8_t>{2}, 5000ms));
    yonetici.push_task(std::make_unique<swarm::ScoutSearchTask>(
            yonetici.drone_state(), 10.0, 0.0));

    yonetici.task_engine_adimi(BASLANGIC);

    swarm::Consensus oy;
    oy.transaction_id(99);
    oy.sender_id(2);
    oy.vote(swarm::Vote::ACK);
    yonetici.add_command(swarm::Command::consensus_oyu(oy));

    yonetici.task_engine_adimi(BASLANGIC + 20ms);

    // Oylama COMMITTED oldu, gorev iptal edilmedi, siradakine gecildi.
    ASSERT_NE(yonetici.current_task(), nullptr);
    EXPECT_EQ(yonetici.current_task()->get_type(), swarm::TaskType::SCOUT_SEARCH);
}

// --- TaskAllocationEngine dogrudan -----------------------------------------

TEST(TaskAllocationEngine, RolEslesmesiniDogruKarar)
{
    swarm::SwarmConfig scout;
    scout.node_type = swarm::NodeType::DRONE;
    scout.role = swarm::DroneRole::SCOUT;

    swarm::SwarmConfig gcs;
    gcs.node_type = swarm::NodeType::GCS;

    const swarm::TaskAllocation scout_emri = gorev_emri_olustur(1, swarm::DroneRole::SCOUT);
    const swarm::TaskAllocation striker_emri = gorev_emri_olustur(2, swarm::DroneRole::STRIKER);

    EXPECT_TRUE(swarm::TaskAllocationEngine::bu_dugumu_ilgilendiriyor(scout_emri, scout));
    EXPECT_FALSE(swarm::TaskAllocationEngine::bu_dugumu_ilgilendiriyor(striker_emri, scout));
    EXPECT_FALSE(swarm::TaskAllocationEngine::bu_dugumu_ilgilendiriyor(scout_emri, gcs));
}
