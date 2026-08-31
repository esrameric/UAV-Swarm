// ============================================================================
//  Faz 1.3 — IDL'den üretilen mesaj tiplerinin testi
//
//  Bu test iki şeyi doğruluyor:
//    1) fastddsgen'in ürettiği C++ sınıfları beklediğimiz alanlara ve
//       varsayılan değerlere sahip mi?
//    2) Enum'ların SAYISAL değerleri planda kararlaştırıldığı gibi mi?
//       Bu kritik: ağda taşınan şey enum'un ismi değil sayısıdır (tel
//       formatı), dolayısıyla sıralamanın kayması sessiz bir uyumsuzluk
//       yaratır.
//
//  Test aynı zamanda bu tiplerin NASIL KULLANILDIĞINI gösteren örnektir:
//  alanlar `nesne.alan(deger)` ile yazılır, `nesne.alan()` ile okunur.
// ============================================================================

#include <gtest/gtest.h>

#include "Consensus.hpp"
#include "ConsensusPubSubTypes.hpp"
#include "Heartbeat.hpp"
#include "HeartbeatPubSubTypes.hpp"
#include "SwarmEnums.hpp"
#include "TaskAllocation.hpp"
#include "Telemetry.hpp"
#include "TelemetryPubSubTypes.hpp"

#include <cstdint>

// ---------------------------------------------------------------------------
//  Enum'ların sayısal değerleri
// ---------------------------------------------------------------------------

TEST(SwarmEnums, NodeTypeDegerleri)
{
    // static_cast<int>: enum class sessizce int'e dönüşmediği için (düz
    // enum'dan farkı budur) dönüşümü açıkça istememiz gerekir.
    EXPECT_EQ(static_cast<int>(swarm::NodeType::DRONE), 0);
    EXPECT_EQ(static_cast<int>(swarm::NodeType::GCS), 1);
}

TEST(SwarmEnums, DroneRoleDegerleri)
{
    EXPECT_EQ(static_cast<int>(swarm::DroneRole::SCOUT), 0);
    EXPECT_EQ(static_cast<int>(swarm::DroneRole::STRIKER), 1);
}

TEST(SwarmEnums, TaskTypeDokuzDegerVeSirasi)
{
    // Bölüm 3.3'teki 9 child task ile birebir örtüşmeli.
    EXPECT_EQ(static_cast<int>(swarm::TaskType::INIT), 0);
    EXPECT_EQ(static_cast<int>(swarm::TaskType::DISCOVERY), 1);
    EXPECT_EQ(static_cast<int>(swarm::TaskType::CONSENSUS), 2);
    EXPECT_EQ(static_cast<int>(swarm::TaskType::IDLE), 3);
    EXPECT_EQ(static_cast<int>(swarm::TaskType::SCOUT_SEARCH), 4);
    EXPECT_EQ(static_cast<int>(swarm::TaskType::GO_TO_TARGET), 5);
    EXPECT_EQ(static_cast<int>(swarm::TaskType::HOVER), 6);
    EXPECT_EQ(static_cast<int>(swarm::TaskType::FAIL_SAFE), 7);
    EXPECT_EQ(static_cast<int>(swarm::TaskType::LANDING), 8);
}

TEST(SwarmEnums, VotePendingSifirOlmali)
{
    // Bölüm 3.5: PENDING'in 0 olması bilinçli bir karar. Default-initialize
    // edilen bir oy alanı otomatik olarak "henüz oy yok" anlamına gelmeli,
    // yanlışlıkla ACK sayılmamalı.
    EXPECT_EQ(static_cast<int>(swarm::Vote::PENDING), 0);
    EXPECT_EQ(static_cast<int>(swarm::Vote::NACK), 1);
    EXPECT_EQ(static_cast<int>(swarm::Vote::ACK), 2);
}

// ---------------------------------------------------------------------------
//  Mesaj alanlarının yazılıp okunması
// ---------------------------------------------------------------------------

TEST(Heartbeat, AlanlarYazilipOkunabiliyor)
{
    swarm::Heartbeat heartbeat;

    heartbeat.drone_id(3);
    heartbeat.node_type(swarm::NodeType::DRONE);
    heartbeat.role(swarm::DroneRole::STRIKER);
    heartbeat.current_task(swarm::TaskType::GO_TO_TARGET);

    EXPECT_EQ(heartbeat.drone_id(), 3u);
    EXPECT_EQ(heartbeat.node_type(), swarm::NodeType::DRONE);
    EXPECT_EQ(heartbeat.role(), swarm::DroneRole::STRIKER);
    EXPECT_EQ(heartbeat.current_task(), swarm::TaskType::GO_TO_TARGET);
}

TEST(Heartbeat, VarsayilanDegerler)
{
    const swarm::Heartbeat heartbeat;

    EXPECT_EQ(heartbeat.drone_id(), 0u);
    EXPECT_EQ(heartbeat.node_type(), swarm::NodeType::DRONE);
    EXPECT_EQ(heartbeat.current_task(), swarm::TaskType::INIT);
}

TEST(Telemetry, KonumHizVeBataryaAlanlari)
{
    swarm::Telemetry telemetry;

    telemetry.drone_id(1);
    telemetry.seq_num(42);
    telemetry.x(10.5);   // Doğu  (metre, ENU)
    telemetry.y(-3.25);  // Kuzey
    telemetry.z(50.0);   // Yukarı
    telemetry.vx(1.5);
    telemetry.vy(0.0);
    telemetry.vz(-0.25);
    telemetry.battery(87);
    telemetry.timestamp(1735689600000ULL);

    EXPECT_EQ(telemetry.drone_id(), 1u);
    EXPECT_EQ(telemetry.seq_num(), 42u);
    EXPECT_DOUBLE_EQ(telemetry.x(), 10.5);
    EXPECT_DOUBLE_EQ(telemetry.y(), -3.25);
    EXPECT_DOUBLE_EQ(telemetry.z(), 50.0);
    EXPECT_DOUBLE_EQ(telemetry.vz(), -0.25);
    EXPECT_EQ(telemetry.battery(), 87u);
    EXPECT_EQ(telemetry.timestamp(), 1735689600000ULL);
}

TEST(Telemetry, SeqNumUint32Genisliginde)
{
    // Bölüm 3.5: seq_num hiç sıfırlanmayan bir uint32_t sayaç. 50 Hz'de
    // taşması için ~2,7 yıl gerekir; bu payın gerçekten var olduğunu
    // en büyük değeri saklayarak doğruluyoruz.
    swarm::Telemetry telemetry;
    telemetry.seq_num(UINT32_MAX);

    EXPECT_EQ(telemetry.seq_num(), 4294967295u);
}

TEST(TaskAllocation, GorevEmriAlanlari)
{
    swarm::TaskAllocation order;

    order.task_id(7);
    order.target_role(swarm::DroneRole::SCOUT);
    order.target_x(120.0);
    order.target_y(-45.5);

    EXPECT_EQ(order.task_id(), 7u);
    EXPECT_EQ(order.target_role(), swarm::DroneRole::SCOUT);
    EXPECT_DOUBLE_EQ(order.target_x(), 120.0);
    EXPECT_DOUBLE_EQ(order.target_y(), -45.5);
}

TEST(Consensus, OyMesajiAlanlari)
{
    swarm::Consensus vote;

    vote.transaction_id(99);
    vote.sender_id(2);
    vote.vote(swarm::Vote::ACK);
    vote.seq_num(5);

    EXPECT_EQ(vote.transaction_id(), 99u);
    EXPECT_EQ(vote.sender_id(), 2u);
    EXPECT_EQ(vote.vote(), swarm::Vote::ACK);
    EXPECT_EQ(vote.seq_num(), 5u);
}

TEST(Consensus, VarsayilanOyPendingOlmali)
{
    // Default-initialize edilen bir Consensus mesajının oyu PENDING olmalı.
    // Bu, "cevap gelmedi" durumunun kazara ACK sayılmasını engelleyen
    // güvenlik ağıdır.
    const swarm::Consensus vote;

    EXPECT_EQ(vote.vote(), swarm::Vote::PENDING);
}

// ---------------------------------------------------------------------------
//  Serileştirme gidiş-dönüşü (round-trip)
// ---------------------------------------------------------------------------

TEST(MesajSerilestirme, HeartbeatGidisDonusAyniKalir)
{
    // fastddsgen her mesaj için bir "PubSubType" sınıfı da üretir; DDS'in
    // mesajı bayta çevirip (serialize) ağa koyarken ve karşı tarafta geri
    // okurken (deserialize) kullandığı sınıf budur. Burada ağa hiç çıkmadan,
    // aynı işlemi yerelde yapıp verinin bozulmadığını doğruluyoruz.
    swarm::HeartbeatPubSubType type_support;

    swarm::Heartbeat sent;
    sent.drone_id(2);
    sent.node_type(swarm::NodeType::GCS);
    sent.role(swarm::DroneRole::STRIKER);
    sent.current_task(swarm::TaskType::CONSENSUS);

    const auto representation = eprosima::fastdds::dds::DEFAULT_DATA_REPRESENTATION;

    // Önce kaç bayt gerektiğini soruyoruz, sonra o kadar yer ayırıyoruz.
    const uint32_t size = type_support.calculate_serialized_size(&sent, representation);
    eprosima::fastdds::rtps::SerializedPayload_t payload(size);

    ASSERT_TRUE(type_support.serialize(&sent, payload, representation));

    swarm::Heartbeat received;
    ASSERT_TRUE(type_support.deserialize(payload, &received));

    EXPECT_EQ(received.drone_id(), sent.drone_id());
    EXPECT_EQ(received.node_type(), sent.node_type());
    EXPECT_EQ(received.role(), sent.role());
    EXPECT_EQ(received.current_task(), sent.current_task());
}

TEST(MesajSerilestirme, TelemetriGidisDonusAyniKalir)
{
    swarm::TelemetryPubSubType type_support;

    swarm::Telemetry sent;
    sent.drone_id(1);
    sent.seq_num(123456);
    sent.x(1.25);
    sent.y(2.5);
    sent.z(3.75);
    sent.vx(-1.0);
    sent.vy(0.5);
    sent.vz(0.125);
    sent.battery(64);
    sent.timestamp(1735689600123ULL);

    const auto representation = eprosima::fastdds::dds::DEFAULT_DATA_REPRESENTATION;
    const uint32_t size = type_support.calculate_serialized_size(&sent, representation);
    eprosima::fastdds::rtps::SerializedPayload_t payload(size);

    ASSERT_TRUE(type_support.serialize(&sent, payload, representation));

    swarm::Telemetry received;
    ASSERT_TRUE(type_support.deserialize(payload, &received));

    EXPECT_EQ(received.seq_num(), sent.seq_num());
    EXPECT_DOUBLE_EQ(received.x(), sent.x());
    EXPECT_DOUBLE_EQ(received.vz(), sent.vz());
    EXPECT_EQ(received.battery(), sent.battery());
    EXPECT_EQ(received.timestamp(), sent.timestamp());
}
