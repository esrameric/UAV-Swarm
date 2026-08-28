// ============================================================================
//  Faz 2.1 — Soyut Task arayüzünün testi
//
//  Soyut bir sınıfın kendisi örneklenemez; test etmenin yolu, ondan türeyen
//  küçük bir "sahte" (fake/test double) child yazıp POLİMORFİZMİN gerçekten
//  çalıştığını göstermektir: Task* üzerinden çağırdığımız fonksiyonlar
//  child'ın gövdesine gitmeli.
// ============================================================================

#include <gtest/gtest.h>

#include "swarm/task/task.hpp"

#include <memory>
#include <string>
#include <vector>

namespace {

// Task'tan türeyen sahte bir görev. `: public Task` KALITIM (inheritance)
// söz dizimidir: "SahteGorev bir Task'tır".
class SahteGorev : public swarm::Task
{
public:
    // `override`: "bu fonksiyon taban sınıftaki sanal bir fonksiyonu eziyor"
    // demektir. İsim veya imza yanlış yazılırsa derleyici hata verir —
    // sessizce yeni bir fonksiyon tanımlamış olmaktan korur.
    void on_enter(swarm::TimePoint) override
    {
        cagri_sirasi.push_back("on_enter");
    }

    void run(swarm::TimePoint) override
    {
        cagri_sirasi.push_back("run");
        ++calisma_sayisi;
    }

    void on_exit() override
    {
        cagri_sirasi.push_back("on_exit");
    }

    bool is_finished() const override
    {
        // İki kez çalıştıktan sonra bittiğini söylesin.
        return calisma_sayisi >= 2;
    }

    swarm::TaskType get_type() const override
    {
        return swarm::TaskType::HOVER;
    }

    int calisma_sayisi = 0;
    std::vector<std::string> cagri_sirasi;
};

}  // namespace

TEST(Task, ChildKendiTipiniBildirir)
{
    const SahteGorev gorev;

    EXPECT_EQ(gorev.get_type(), swarm::TaskType::HOVER);
}

TEST(Task, YasamDongusuSiraylaCalisir)
{
    SahteGorev gorev;
    const swarm::TimePoint simdi{};

    gorev.on_enter(simdi);
    gorev.run(simdi);
    gorev.run(simdi);
    gorev.on_exit();

    const std::vector<std::string> beklenen{"on_enter", "run", "run", "on_exit"};
    EXPECT_EQ(gorev.cagri_sirasi, beklenen);
}

TEST(Task, IsFinishedBaslangictaFalse)
{
    const SahteGorev gorev;

    EXPECT_FALSE(gorev.is_finished());
}

TEST(Task, PolimorfizmTabanIsaretcisiUzerindenCalisir)
{
    // ASIL MESELE BU: Task Engine, elindeki görevin hangi child olduğunu
    // bilmez; hepsini `Task*` olarak tutar. Buna rağmen çağrılar doğru
    // child'ın gövdesine gitmelidir.
    //
    // std::unique_ptr: AKILLI İŞARETÇİ (smart pointer). İşaret ettiği
    // nesnenin tek sahibidir ve kapsam dışına çıkınca onu otomatik siler.
    // `delete` yazmayı unutma ihtimalini ortadan kaldırır (RAII).
    std::unique_ptr<swarm::Task> gorev = std::make_unique<SahteGorev>();
    const swarm::TimePoint simdi{};

    EXPECT_EQ(gorev->get_type(), swarm::TaskType::HOVER);
    EXPECT_FALSE(gorev->is_finished());

    gorev->on_enter(simdi);
    gorev->run(simdi);
    EXPECT_FALSE(gorev->is_finished());

    gorev->run(simdi);
    EXPECT_TRUE(gorev->is_finished());
}

TEST(Task, FarkliChildlarAyniKaptaTutulabilir)
{
    // Polimorfizmin pratik faydası: farklı türden görevler tek bir kuyrukta
    // yan yana durabilir ve aynı arayüzle işletilebilir.
    std::vector<std::unique_ptr<swarm::Task>> kuyruk;
    kuyruk.push_back(std::make_unique<SahteGorev>());
    kuyruk.push_back(std::make_unique<SahteGorev>());

    const swarm::TimePoint simdi{};
    for (auto& gorev : kuyruk)
    {
        gorev->on_enter(simdi);
        gorev->run(simdi);
    }

    EXPECT_EQ(kuyruk.size(), 2u);
    EXPECT_EQ(kuyruk[0]->get_type(), swarm::TaskType::HOVER);
}
