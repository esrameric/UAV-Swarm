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
    swarm::Heartbeat kalp_atisi;

    kalp_atisi.drone_id(3);
    kalp_atisi.node_type(swarm::NodeType::DRONE);
    kalp_atisi.role(swarm::DroneRole::STRIKER);
    kalp_atisi.current_task(swarm::TaskType::GO_TO_TARGET);

    EXPECT_EQ(kalp_atisi.drone_id(), 3u);
    EXPECT_EQ(kalp_atisi.node_type(), swarm::NodeType::DRONE);
    EXPECT_EQ(kalp_atisi.role(), swarm::DroneRole::STRIKER);
    EXPECT_EQ(kalp_atisi.current_task(), swarm::TaskType::GO_TO_TARGET);
}

TEST(Heartbeat, VarsayilanDegerler)
{
    const swarm::Heartbeat kalp_atisi;

    EXPECT_EQ(kalp_atisi.drone_id(), 0u);
    EXPECT_EQ(kalp_atisi.node_type(), swarm::NodeType::DRONE);
    EXPECT_EQ(kalp_atisi.current_task(), swarm::TaskType::INIT);
}

TEST(Telemetry, KonumHizVeBataryaAlanlari)
{
    swarm::Telemetry telemetri;

    telemetri.drone_id(1);
    telemetri.seq_num(42);
    telemetri.x(10.5);   // Doğu  (metre, ENU)
    telemetri.y(-3.25);  // Kuzey
    telemetri.z(50.0);   // Yukarı
    telemetri.vx(1.5);
    telemetri.vy(0.0);
    telemetri.vz(-0.25);
    telemetri.battery(87);
    telemetri.timestamp(1735689600000ULL);

    EXPECT_EQ(telemetri.drone_id(), 1u);
    EXPECT_EQ(telemetri.seq_num(), 42u);
    EXPECT_DOUBLE_EQ(telemetri.x(), 10.5);
    EXPECT_DOUBLE_EQ(telemetri.y(), -3.25);
    EXPECT_DOUBLE_EQ(telemetri.z(), 50.0);
    EXPECT_DOUBLE_EQ(telemetri.vz(), -0.25);
    EXPECT_EQ(telemetri.battery(), 87u);
    EXPECT_EQ(telemetri.timestamp(), 1735689600000ULL);
}

TEST(Telemetry, SeqNumUint32Genisliginde)
{
    // Bölüm 3.5: seq_num hiç sıfırlanmayan bir uint32_t sayaç. 50 Hz'de
    // taşması için ~2,7 yıl gerekir; bu payın gerçekten var olduğunu
    // en büyük değeri saklayarak doğruluyoruz.
    swarm::Telemetry telemetri;
    telemetri.seq_num(UINT32_MAX);

    EXPECT_EQ(telemetri.seq_num(), 4294967295u);
}

TEST(TaskAllocation, GorevEmriAlanlari)
{
    swarm::TaskAllocation emir;

    emir.task_id(7);
    emir.target_role(swarm::DroneRole::SCOUT);
    emir.target_x(120.0);
    emir.target_y(-45.5);

    EXPECT_EQ(emir.task_id(), 7u);
    EXPECT_EQ(emir.target_role(), swarm::DroneRole::SCOUT);
    EXPECT_DOUBLE_EQ(emir.target_x(), 120.0);
    EXPECT_DOUBLE_EQ(emir.target_y(), -45.5);
}

TEST(Consensus, OyMesajiAlanlari)
{
    swarm::Consensus oy;

    oy.transaction_id(99);
    oy.sender_id(2);
    oy.vote(swarm::Vote::ACK);
    oy.seq_num(5);

    EXPECT_EQ(oy.transaction_id(), 99u);
    EXPECT_EQ(oy.sender_id(), 2u);
    EXPECT_EQ(oy.vote(), swarm::Vote::ACK);
    EXPECT_EQ(oy.seq_num(), 5u);
}

TEST(Consensus, VarsayilanOyPendingOlmali)
{
    // Default-initialize edilen bir Consensus mesajının oyu PENDING olmalı.
    // Bu, "cevap gelmedi" durumunun kazara ACK sayılmasını engelleyen
    // güvenlik ağıdır.
    const swarm::Consensus oy;

    EXPECT_EQ(oy.vote(), swarm::Vote::PENDING);
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
    swarm::HeartbeatPubSubType tip_destegi;

    swarm::Heartbeat gonderilen;
    gonderilen.drone_id(2);
    gonderilen.node_type(swarm::NodeType::GCS);
    gonderilen.role(swarm::DroneRole::STRIKER);
    gonderilen.current_task(swarm::TaskType::CONSENSUS);

    const auto gosterim = eprosima::fastdds::dds::DEFAULT_DATA_REPRESENTATION;

    // Önce kaç bayt gerektiğini soruyoruz, sonra o kadar yer ayırıyoruz.
    const uint32_t boyut = tip_destegi.calculate_serialized_size(&gonderilen, gosterim);
    eprosima::fastdds::rtps::SerializedPayload_t yuk(boyut);

    ASSERT_TRUE(tip_destegi.serialize(&gonderilen, yuk, gosterim));

    swarm::Heartbeat alinan;
    ASSERT_TRUE(tip_destegi.deserialize(yuk, &alinan));

    EXPECT_EQ(alinan.drone_id(), gonderilen.drone_id());
    EXPECT_EQ(alinan.node_type(), gonderilen.node_type());
    EXPECT_EQ(alinan.role(), gonderilen.role());
    EXPECT_EQ(alinan.current_task(), gonderilen.current_task());
}

TEST(MesajSerilestirme, TelemetriGidisDonusAyniKalir)
{
    swarm::TelemetryPubSubType tip_destegi;

    swarm::Telemetry gonderilen;
    gonderilen.drone_id(1);
    gonderilen.seq_num(123456);
    gonderilen.x(1.25);
    gonderilen.y(2.5);
    gonderilen.z(3.75);
    gonderilen.vx(-1.0);
    gonderilen.vy(0.5);
    gonderilen.vz(0.125);
    gonderilen.battery(64);
    gonderilen.timestamp(1735689600123ULL);

    const auto gosterim = eprosima::fastdds::dds::DEFAULT_DATA_REPRESENTATION;
    const uint32_t boyut = tip_destegi.calculate_serialized_size(&gonderilen, gosterim);
    eprosima::fastdds::rtps::SerializedPayload_t yuk(boyut);

    ASSERT_TRUE(tip_destegi.serialize(&gonderilen, yuk, gosterim));

    swarm::Telemetry alinan;
    ASSERT_TRUE(tip_destegi.deserialize(yuk, &alinan));

    EXPECT_EQ(alinan.seq_num(), gonderilen.seq_num());
    EXPECT_DOUBLE_EQ(alinan.x(), gonderilen.x());
    EXPECT_DOUBLE_EQ(alinan.vz(), gonderilen.vz());
    EXPECT_EQ(alinan.battery(), gonderilen.battery());
    EXPECT_EQ(alinan.timestamp(), gonderilen.timestamp());
}
