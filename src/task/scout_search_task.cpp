#include "swarm/task/scout_search_task.hpp"

#include "swarm/task/motion.hpp"

namespace swarm {

ScoutSearchTask::ScoutSearchTask(
        DroneState& state,
        double center_x,
        double center_y,
        std::chrono::milliseconds scan_duration)
    : state_(state)
    , center_x_(center_x)
    , center_y_(center_y)
    , scan_duration_(scan_duration)
{
}

void ScoutSearchTask::on_enter(TimePoint now)
{
    last_update_ = now;
    region_reached_ = false;
    finished_ = false;
}

void ScoutSearchTask::run(TimePoint now)
{
    const double elapsed_seconds =
            std::chrono::duration<double>(now - last_update_).count();
    last_update_ = now;

    // --- 1. aşama: arama bölgesine git ---
    if (!region_reached_)
    {
        region_reached_ = move_toward_target(
                state_, center_x_, center_y_,
                HORIZONTAL_SPEED_M_S, ARRIVAL_TOLERANCE_M, elapsed_seconds);

        if (region_reached_)
        {
            // Tarama sayacı, bölgeye VARDIĞIMIZ anda başlar; görevin
            // başında değil. Uzak bir bölgeye uçuş süresi taramadan sayılmaz.
            scan_start_ = now;
        }
        return;
    }

    // --- 2. aşama: bölgede tara ---
    state_.reset_velocity();

    const auto scan_elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - scan_start_);

    if (scan_elapsed >= scan_duration_)
    {
        finished_ = true;
    }
}

void ScoutSearchTask::on_exit()
{
}

bool ScoutSearchTask::is_finished() const
{
    return finished_;
}

TaskType ScoutSearchTask::get_type() const
{
    return TaskType::SCOUT_SEARCH;
}

}  // namespace swarm
