// GoToTargetTask — Müdahale (STRIKER) drone'larının görevi.
// Verilen hedef koordinatına doğru sabit hızla ilerler, varınca biter.
#pragma once

#include "swarm/drone_state.hpp"
#include "swarm/task/task.hpp"

namespace swarm {

class GoToTargetTask : public Task
{
public:
    static constexpr double YATAY_HIZ_M_S = 5.0;
    static constexpr double VARIS_TOLERANSI_M = 0.5;

    GoToTargetTask(DroneState& durum, double hedef_x, double hedef_y);

    void on_enter(TimePoint now) override;
    void run(TimePoint now) override;
    void on_exit() override;
    bool is_finished() const override;
    TaskType get_type() const override;

    double hedef_x() const { return hedef_x_; }
    double hedef_y() const { return hedef_y_; }

private:
    DroneState& durum_;
    double hedef_x_;
    double hedef_y_;
    TimePoint son_calisma_{};
    bool tamamlandi_ = false;
};

}  // namespace swarm
