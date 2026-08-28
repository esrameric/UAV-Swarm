// HoverTask — verilen süre boyunca havada sabit durur, sonra biter.
#pragma once

#include <chrono>

#include "swarm/drone_state.hpp"
#include "swarm/task/task.hpp"

namespace swarm {

class HoverTask : public Task
{
public:
    HoverTask(DroneState& durum, std::chrono::milliseconds sure);

    void on_enter(TimePoint now) override;
    void run(TimePoint now) override;
    void on_exit() override;
    bool is_finished() const override;
    TaskType get_type() const override;

private:
    DroneState& durum_;
    std::chrono::milliseconds sure_;
    TimePoint baslangic_{};
    bool tamamlandi_ = false;
};

}  // namespace swarm
