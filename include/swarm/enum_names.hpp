// ============================================================================
//  Enum -> okunabilir metin dönüşümleri
//
//  Enum'lar ağda sayı olarak taşınır ama log'da "3" görmek hiçbir şey
//  anlatmaz. Bu fonksiyonlar log ve hata mesajları için insan tarafından
//  okunabilir karşılıkları üretir.
//
//  Faz 6'daki entegrasyon testleri bu metinlere göre log arayacağı için
//  isimler kararlıdır, keyfi değiştirilmemelidir.
// ============================================================================

#pragma once

#include "SwarmEnums.hpp"

namespace swarm {

inline const char* node_type_name(NodeType type)
{
    switch (type)
    {
        case NodeType::DRONE: return "DRONE";
        case NodeType::GCS:   return "GCS";
    }
    return "BILINMIYOR";
}

inline const char* drone_role_name(DroneRole role)
{
    switch (role)
    {
        case DroneRole::SCOUT:   return "SCOUT";
        case DroneRole::STRIKER: return "STRIKER";
    }
    return "BILINMIYOR";
}

inline const char* task_type_name(TaskType type)
{
    switch (type)
    {
        case TaskType::INIT:          return "INIT";
        case TaskType::DISCOVERY:     return "DISCOVERY";
        case TaskType::CONSENSUS:     return "CONSENSUS";
        case TaskType::IDLE:          return "IDLE";
        case TaskType::SCOUT_SEARCH:  return "SCOUT_SEARCH";
        case TaskType::GO_TO_TARGET:  return "GO_TO_TARGET";
        case TaskType::HOVER:         return "HOVER";
        case TaskType::FAIL_SAFE:     return "FAIL_SAFE";
        case TaskType::LANDING:       return "LANDING";
    }
    return "BILINMIYOR";
}

inline const char* vote_name(Vote vote)
{
    switch (vote)
    {
        case Vote::PENDING: return "PENDING";
        case Vote::NACK:    return "NACK";
        case Vote::ACK:     return "ACK";
    }
    return "BILINMIYOR";
}

}  // namespace swarm
