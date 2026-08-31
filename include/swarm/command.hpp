// ============================================================================
//  Command — ağdan gelip işlenmeyi bekleyen bir komut
//
//  Komut queue'suna (command_queue) iki tür mesaj girer: görev dağıtım emri
//  (TaskAllocation) ve consensus oyu (Consensus). İkisini tek bir queue'da
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
    static Command task_order(const TaskAllocation& order)
    {
        Command command;
        command.type = CommandType::TASK_ALLOCATION;
        command.task_allocation = order;
        return command;
    }

    static Command consensus_vote(const Consensus& vote)
    {
        Command command;
        command.type = CommandType::CONSENSUS;
        command.consensus = vote;
        return command;
    }
};

}  // namespace swarm
