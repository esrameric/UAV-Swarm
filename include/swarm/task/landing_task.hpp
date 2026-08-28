// LandingTask — sabit hızla alçalır, yere değince biter.
#pragma once

#include "swarm/drone_state.hpp"
#include "swarm/task/task.hpp"

namespace swarm {

class LandingTask : public Task
{
public:
    // İniş hızı, metre/saniye. Gerçek bir uçuş kontrolcüsü olmadığı için
    // (SITL) sabit seçildi; okunabilirlik adına isimlendirilmiş sabit.
    static constexpr double INIS_HIZI_M_S = 1.0;

    explicit LandingTask(DroneState& durum);

    void on_enter(TimePoint now) override;
    void run(TimePoint now) override;
    void on_exit() override;
    bool is_finished() const override;
    TaskType get_type() const override;

private:
    DroneState& durum_;

    // Hareket hesabı için "son çağrıdan bu yana kaç saniye geçti" bilgisi
    // gerekir; run() mutlak zaman aldığı için farkı kendimiz tutuyoruz.
    TimePoint son_calisma_{};
    bool tamamlandi_ = false;
};

}  // namespace swarm
