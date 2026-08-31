// FailSafeTask — arıza/acil durum davranışı.
//
// Tetiklendiğinde (örn. bir peer'ın kaybolması, batarya kritiği) aracı
// ANINDA durdurur ve kısa bir "değerlendirme" süresi bekler. Süre sonunda
// biter; Task Engine bunun ardından LandingTask'ı işletir.
//
// Not: Aracı hemen indirmek yerine önce durdurup kısa süre beklemek bilinçli
// bir seçim — geçici bir ağ kesintisi kalıcı bir arıza gibi davranıp
// sürüyü gereksiz yere yere indirmesin diye.
#pragma once

#include <chrono>

#include "swarm/drone_state.hpp"
#include "swarm/task/task.hpp"

namespace swarm {

class FailSafeTask : public Task
{
public:
    static constexpr std::chrono::milliseconds DEFAULT_ASSESSMENT_DURATION{1000};

    explicit FailSafeTask(
            DroneState& state,
            std::chrono::milliseconds assessment_duration = DEFAULT_ASSESSMENT_DURATION);

    void on_enter(TimePoint now) override;
    void run(TimePoint now) override;
    void on_exit() override;
    bool is_finished() const override;
    TaskType get_type() const override;

private:
    DroneState& state_;
    std::chrono::milliseconds assessment_duration_;
    TimePoint start_{};
    bool finished_ = false;
};

}  // namespace swarm
