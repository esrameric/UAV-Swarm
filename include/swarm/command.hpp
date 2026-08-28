// ============================================================================
//  Command — ağdan gelip işlenmeyi bekleyen bir komut
//
//  Komut kuyruğuna (command_queue) iki tür mesaj girer: görev dağıtım emri
//  (TaskAllocation) ve consensus oyu (Consensus). İkisini tek bir kuyrukta
//  taşıyabilmek için ortak bir kap tipine ihtiyaç var.
//
//  Neden `std::variant` değil? C++17'nin variant'ı bu iş için "doğru" araç
//  sayılabilir ama okuması ve kullanması (std::visit, std::get_if) yeni
//  başlayan biri için ağır. Burada okunabilirlik önceliklidir: hangi alanın
//  geçerli olduğunu söyleyen basit bir `type` etiketi kullanıyoruz.
// ============================================================================

#pragma once

#include "Consensus.hpp"
#include "TaskAllocation.hpp"

namespace swarm {

enum class CommandType
{
    TASK_ALLOCATION,   // task_allocation alanı geçerli
    CONSENSUS          // consensus alanı geçerli
};

struct Command
{
    // Hangi alanın dolu olduğunu söyleyen etiket. Yanlış alanı okumamak
    // için önce buna bakılır.
    CommandType type = CommandType::TASK_ALLOCATION;

    TaskAllocation task_allocation{};
    Consensus consensus{};

    // Okunabilir kurucular: `Command::gorev_emri(emir)` yazımı,
    // alanları elle doldurmaktan daha az hata yapılır.
    static Command gorev_emri(const TaskAllocation& emir)
    {
        Command komut;
        komut.type = CommandType::TASK_ALLOCATION;
        komut.task_allocation = emir;
        return komut;
    }

    static Command consensus_oyu(const Consensus& oy)
    {
        Command komut;
        komut.type = CommandType::CONSENSUS;
        komut.consensus = oy;
        return komut;
    }
};

}  // namespace swarm
