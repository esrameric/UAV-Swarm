// GoToTargetTask — Müdahale (STRIKER) drone'larının görevi.
// Verilen hedef koordinatına doğru sabit hızla ilerler, varınca biter.
#pragma once

#include "swarm/drone_state.hpp"
#include "swarm/task/task.hpp"

namespace swarm {

class GoToTargetTask : public Task
{
public:
    static constexpr double HORIZONTAL_SPEED_M_S = 5.0;
    static constexpr double ARRIVAL_TOLERANCE_M = 0.5;

    GoToTargetTask(DroneState& state, double target_x, double target_y);

    void on_enter(TimePoint now) override;
    void run(TimePoint now) override;
    void on_exit() override;
    bool is_finished() const override;
    TaskType get_type() const override;

    double target_x() const { return target_x_; }
    double target_y() const { return target_y_; }

private:
    DroneState& state_;
    double target_x_;
    double target_y_;
    TimePoint last_update_{};
    bool finished_ = false;
};

}  // namespace swarm
