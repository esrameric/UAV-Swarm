// HoverTask — verilen süre boyunca havada sabit durur, sonra biter.
#pragma once

#include <chrono>

#include "swarm/drone_state.hpp"
#include "swarm/task/task.hpp"

namespace swarm {

class HoverTask : public Task
{
public:
    HoverTask(DroneState& state, std::chrono::milliseconds duration);

    void on_enter(TimePoint now) override;
    void run(TimePoint now) override;
    void on_exit() override;
    bool is_finished() const override;
    TaskType get_type() const override;

private:
    DroneState& state_;
    std::chrono::milliseconds duration_;
    TimePoint start_{};
    bool finished_ = false;
};

}  // namespace swarm
