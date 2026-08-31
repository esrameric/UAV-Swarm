#include "swarm/task/init_task.hpp"

namespace swarm {

// Parametre ADI YAZILMADI. -Wextra ile derlerken kullanılmayan parametre
// uyarı üretir; ismi vermemek "bu parametreyi bilerek kullanmıyorum"
// demenin standart yoludur.
void InitTask::on_enter(TimePoint)
{
    finished_ = false;
}

void InitTask::run(TimePoint)
{
    // Başlangıç işleri (FastDDS kurulumu, kimlik okuma) SwarmManager::init()
    // içinde zaten yapılıyor; bu task yalnızca queue'nun tanımlı bir
    // başlangıcı olsun diye var ve ilk turda tamamlanır.
    finished_ = true;
}

void InitTask::on_exit()
{
}

bool InitTask::is_finished() const
{
    return finished_;
}

TaskType InitTask::get_type() const
{
    return TaskType::INIT;
}

}  // namespace swarm
