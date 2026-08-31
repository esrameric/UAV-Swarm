// ============================================================================
//  Faz 2.1 — Soyut Task interface'inin testi
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
class FakeTask : public swarm::Task
{
public:
    // `override`: "bu fonksiyon taban sınıftaki sanal bir fonksiyonu eziyor"
    // demektir. İsim veya imza yanlış yazılırsa derleyici hata verir —
    // sessizce yeni bir fonksiyon tanımlamış olmaktan korur.
    void on_enter(swarm::TimePoint) override
    {
        call_order.push_back("on_enter");
    }

    void run(swarm::TimePoint) override
    {
        call_order.push_back("run");
        ++run_count;
    }

    void on_exit() override
    {
        call_order.push_back("on_exit");
    }

    bool is_finished() const override
    {
        // İki kez çalıştıktan sonra bittiğini söylesin.
        return run_count >= 2;
    }

    swarm::TaskType get_type() const override
    {
        return swarm::TaskType::HOVER;
    }

    int run_count = 0;
    std::vector<std::string> call_order;
};

}  // namespace

TEST(Task, ChildKendiTipiniBildirir)
{
    const FakeTask task;

    EXPECT_EQ(task.get_type(), swarm::TaskType::HOVER);
}

TEST(Task, YasamDongusuSiraylaCalisir)
{
    FakeTask task;
    const swarm::TimePoint current_time{};

    task.on_enter(current_time);
    task.run(current_time);
    task.run(current_time);
    task.on_exit();

    const std::vector<std::string> expected{"on_enter", "run", "run", "on_exit"};
    EXPECT_EQ(task.call_order, expected);
}

TEST(Task, IsFinishedBaslangictaFalse)
{
    const FakeTask task;

    EXPECT_FALSE(task.is_finished());
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
    std::unique_ptr<swarm::Task> task = std::make_unique<FakeTask>();
    const swarm::TimePoint current_time{};

    EXPECT_EQ(task->get_type(), swarm::TaskType::HOVER);
    EXPECT_FALSE(task->is_finished());

    task->on_enter(current_time);
    task->run(current_time);
    EXPECT_FALSE(task->is_finished());

    task->run(current_time);
    EXPECT_TRUE(task->is_finished());
}

TEST(Task, FarkliChildlarAyniKaptaTutulabilir)
{
    // Polimorfizmin pratik faydası: farklı türden görevler tek bir queue'da
    // yan yana durabilir ve aynı interface'le işletilebilir.
    std::vector<std::unique_ptr<swarm::Task>> queue;
    queue.push_back(std::make_unique<FakeTask>());
    queue.push_back(std::make_unique<FakeTask>());

    const swarm::TimePoint current_time{};
    for (auto& task : queue)
    {
        task->on_enter(current_time);
        task->run(current_time);
    }

    EXPECT_EQ(queue.size(), 2u);
    EXPECT_EQ(queue[0]->get_type(), swarm::TaskType::HOVER);
}
