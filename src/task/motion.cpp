#include "swarm/task/motion.hpp"

#include <cmath>

namespace swarm {

double horizontal_distance(double x1, double y1, double x2, double y2)
{
    const double delta_x = x2 - x1;
    const double delta_y = y2 - y1;

    // std::hypot(a, b) = sqrt(a*a + b*b), ama ara sonuçlarda taşma/alttaşma
    // yaşamadan hesaplar. Elle sqrt yazmaya tercih edilir.
    return std::hypot(delta_x, delta_y);
}

bool move_toward_target(
        DroneState& state,
        double target_x,
        double target_y,
        double speed_meters_per_second,
        double tolerance_meters,
        double elapsed_seconds)
{
    const double remaining = horizontal_distance(state.x, state.y, target_x, target_y);

    if (remaining <= tolerance_meters)
    {
        state.reset_velocity();
        return true;
    }

    const double step = speed_meters_per_second * elapsed_seconds;

    if (step >= remaining)
    {
        // Bu adımda hedefe varıyoruz. Hedefi GEÇMEYİP tam üstüne oturuyoruz;
        // aksi halde araç hedefin etrafında ileri geri salınırdı.
        state.x = target_x;
        state.y = target_y;
        state.reset_velocity();
        return true;
    }

    // Hedefe doğru birim vektör: yön bilgisini uzunluğundan ayırıyoruz.
    const double direction_x = (target_x - state.x) / remaining;
    const double direction_y = (target_y - state.y) / remaining;

    state.x += direction_x * step;
    state.y += direction_y * step;

    state.vx = direction_x * speed_meters_per_second;
    state.vy = direction_y * speed_meters_per_second;
    state.vz = 0.0;

    return false;
}

}  // namespace swarm
