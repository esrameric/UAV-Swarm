# Sürü İHA Sistemi (Swarm UAV)

3 drone'luk **heterojen** bir sürü İHA sisteminin yazılım mimarisi. Gerçek
donanım yok — her şey SITL (Software-in-the-Loop) olarak, Ubuntu 22.04 +
Docker üzerinde çalışıyor.

Sistem, internetin/bulutun olmadığı veya güvenilmediği saha senaryolarına
dayanıklı olacak şekilde **fiziksel bir Taktik Yer Kontrol İstasyonu (YKİ /
GCS)** üzerinden yönetilir. Düğümler birbirleriyle **saf (native) C++ Fast DDS**
üzerinden, tam örgü (full mesh) topolojide, gerçek UDP/TCP ile haberleşir.
**ROS 2 kullanılmıyor.**

| Düğüm | NodeType | DroneRole | Sorumluluk |
|---|---|---|---|
| GCS (Taktik YKİ) | `GCS` | — | Görev emri verir, consensus başlatır, telemetri izler |
| Drone 1 | `DRONE` | `SCOUT` | Alan taraması / hedef tespiti |
| Drone 2 | `DRONE` | `STRIKER` | Hedefe müdahale |
| Drone 3 | `DRONE` | `STRIKER` | Hedefe müdahale |

> Bu depo aynı zamanda bir **C++ öğrenme projesidir**. Mimari bilinçli olarak
> tam kapsamlı tutulmuştur; buna karşılık kod, hiç C++ bilmeyen birinin
> okuyabilmesi için bol yorumlu ve sade yazılmıştır. Her yeni kavram, kod
> tabanında **ilk geçtiği yerde** açıklanır ve aşağıdaki
> [Kavramlar Sözlüğü](#kavramlar-sözlüğü)'ne eklenir.

---

## İçindekiler

- [Kurulum](#kurulum)
- [Derleme ve Test](#derleme-ve-test)
- [Dizin Yapısı](#dizin-yapısı)
- [Varsayımlar](#varsayımlar)
- [Kavramlar Sözlüğü](#kavramlar-sözlüğü)

---

## Kurulum

Fast DDS, Ubuntu 22.04'ün apt depolarında hazır paket olarak **yoktur**;
kaynaktan derlenir. Aşağıdaki script bunu uçtan uca yapar (sürümler
`tools/fastdds.repos` içinde pinlenmiştir):

```bash
bash tools/install_fastdds.sh
```

Kurulum bittikten sonra **her yeni terminalde** Fast DDS'i ortama almak için:

```bash
source tools/fastdds_env.sh
```

### Ortam Doğrulaması (Faz 0)

Kurulumun sağlam olduğunu uçtan uca kanıtlayan script'ler. Her biri sonunda
`SONUC: BASARILI` / `SONUC: BASARISIZ` basar ve buna uygun çıkış kodu döner:

| Script | Ne doğrular |
|---|---|
| `tools/verify_fastddsgen.sh` | fastddsgen bir `.idl` dosyasını gerçekten C++'a çevirebiliyor mu (Java bağımlılığı dahil) |
| `tools/verify_hello_world.sh` | İki ayrı süreç DDS discovery ile buluşup veri akıtabiliyor mu |
| `tools/verify_docker.sh` | Docker, Docker Compose v2, daemon erişimi, custom bridge ağı |
| `tools/verify_multicast.sh` | **Kritik:** custom bridge ağında UDP multicast gerçekten teslim ediliyor mu |

`docker` grubunda değilseniz docker'lı olanları şöyle çalıştırın:

```bash
DOCKER="sudo docker" bash tools/verify_multicast.sh
```

## Derleme ve Test

```bash
cmake -B build -S .
cmake --build build -j3
ctest --test-dir build --output-on-failure
```

## Dizin Yapısı

```
uav-swarm/
├── idl/                  # Fast DDS mesaj tanımları (.idl)
├── include/swarm/        # Başlık (header) dosyaları
│   └── task/             #   Task hiyerarşisi başlıkları
├── src/                  # Kaynak (.cpp) dosyaları
│   └── task/             #   Task hiyerarşisi gövdeleri
├── config/               # Fast DDS QoS XML profilleri
├── docker/               # Dockerfile + docker-compose.yml
├── tests/
│   ├── unit/             # GoogleTest birim testleri
│   └── integration/      # docker-compose tabanlı SITL entegrasyon testleri
├── tools/                # Ortam kurulum/doğrulama script'leri (Faz 0)
│   └── multicast_check/  #   multicast sondaları (Faz 0.5)
└── CMakeLists.txt
```

---

## Varsayımlar

Plan dokümanında açıkça belirtilmemiş, uygulama sırasında karar verilen
noktalar burada toplanır. Amaç: kodu inceleyen kişinin "burada neden böyle
yapılmış?" sorusunun cevabını tek yerde bulması.

| # | Konu | Varsayım / Karar | Gerekçe |
|---|---|---|---|
| V1 | Fast DDS sürümü | `v3.6.2` (Fast-CDR `v2.3.6`, fastddsgen `v4.3.0`, foonathan_memory_vendor `v1.4.1`) | Plan "en güncel kararlı sürüm, Faz 0'da pinlenir" diyordu. v3.6.2'nin kendi `fastdds.repos` dosyası zaten tam pinli olduğu için birebir o kullanıldı. |
| V2 | Fast DDS kurulum yeri | Sisteme (`/usr/local`) değil, kullanıcı-yerel `~/Fast-DDS/install` altına | eProsima'nın önerdiği colcon workspace akışı bu; ayrıca root yetkisi yalnızca apt paketleri için gerekiyor. |
| V3 | `tools/` dizini | Plandaki dizin ağacında yok, eklendi | Faz 0'ın ortam doğrulama adımlarının (kurulum, multicast testi vb.) tekrar edilebilir olması için script olarak depoda tutuluyor. |
| V4 | Üretilen IDL kodu | Depoya commit **edilmez**, derleme sırasında `fastddsgen` ile üretilir (`.gitignore`'da `generated/`) | Üretilen kod ile `.idl` kaynağının birbirinden ayrışma (drift) riskini ortadan kaldırır. |
| V5 | Docker erişimi | Doğrulama script'leri `docker` komutunu `DOCKER` ortam değişkeninden okur (varsayılan: `docker`) | Kullanıcı `docker` grubunda değilse script'i değiştirmeden `DOCKER="sudo docker" bash tools/...` ile çalıştırabilmek için. |
| V6 | ROS 2 çakışması | Derleme script'leri `PATH`'ten `/opt/ros/*` girdilerini temizler | Geliştirme makinesinde ROS 2 Humble kurulu. CMake, `PATH`'teki `.../bin` girdilerinin üst dizinini de `find_package` ön eki olarak tarar; bu yüzden ROS 2'nin `ament` paketleri Fast DDS derlemesine sızıp hata veriyordu. Proje ROS 2 kullanmıyor (Bölüm 2). |
| V7 | Multicast sondası | Faz 0.5 testi `python:3.12-alpine` imajı + iki küçük Python script'i ile yapılır | Ortam testi C++ kodundan bağımsız olmalı; Python'ın `socket` modülü multicast join/send işini 10 satırda, ek bağımlılık olmadan yapıyor. |
| V8 | Multicast adresi | Sondalar `239.255.0.1:7400` kullanır | Fast DDS'in varsayılan SPDP discovery adresi/portu; testin gerçek kullanım senaryosuyla birebir örtüşmesi için. |

---

## Kavramlar Sözlüğü

Kod tabanında geçen C++ ve mimari kavramların sade Türkçe açıklamaları.
Her faz yeni bir kavram getirdikçe bu liste büyür.

### Derleme ve Test Araçları

**CMake** — C++'ta "hangi dosyalar derlenecek, hangi kütüphanelere bağlanacak"
bilgisini tarif ettiğimiz build sistemi. `CMakeLists.txt` dosyasını okur ve
işletim sistemine uygun derleme dosyalarını (Makefile vb.) üretir. Derleyiciyi
her seferinde elle uzun komutlarla çağırmaktan kurtarır.

**GoogleTest (gtest)** — C++ için birim test kütüphanesi. `TEST(Grup, Ad) { }`
bloklarıyla test senaryoları yazılır; içindeki `EXPECT_EQ` / `ASSERT_TRUE` gibi
kontroller sağlanmazsa test başarısız olur. Bu projede testler yalnızca
doğrulama değil, ilgili sınıfın **nasıl kullanıldığını gösteren örnek** olarak
da yazılır.

**CTest** — CMake ile gelen test koşucusu. `add_test()` ile kaydedilen tüm
testleri `ctest` komutuyla topluca çalıştırır ve özet rapor verir.

**Birim test / Entegrasyon testi** — Birim test tek bir sınıfı veya fonksiyonu
izole şekilde, saniyenin altında sınar (`tests/unit/`). Entegrasyon testi ise
sistemin tamamını gerçek koşullarında — burada 4 Docker container'ı gerçek ağ
üzerinde — ayağa kaldırıp uçtan uca doğrular (`tests/integration/`).

### C++ Temelleri

**`#include`** — Başka bir dosyanın içeriğini bulunduğu yere kopyalayan
önişlemci (preprocessor) komutu. Açılı parantez `<...>` sistem/kütüphane
başlıkları, çift tırnak `"..."` projenin kendi başlıkları için kullanılır.

**C++ standardı (C++17)** — Dilin sürümüdür. Bu proje C++17 kullanır; derleyici
hangi standartta çalıştığını `__cplusplus` makrosuyla bildirir (C++17 için
`201703L`).

**`static_assert`** — Bir koşulu **derleme zamanında** kontrol eder; koşul
sağlanmazsa program hiç derlenmez. Çalışma zamanına kalmadan hata yakalamanın
en ucuz yoludur.

### Ağ ve Konteyner

**Docker / container** — Bir uygulamayı, bağımlılıklarıyla birlikte izole ve
tekrar edilebilir bir paket halinde çalıştırma teknolojisi. Sanal makineden
farkı: kendi çekirdeğini (kernel) taşımaz, host'unkini paylaşır — bu yüzden
saniyeler içinde başlar. Bu projede 4 düğüm (1 GCS + 3 drone) 4 ayrı
container olarak çalışır.

**Docker image / Dockerfile** — Image, container'ın dondurulmuş şablonudur;
Dockerfile ise o image'ın nasıl kurulacağını anlatan tarif dosyası. Bu projede
**tek bir image** üretilir, 4 container aynı image'dan ortam değişkenleriyle
(`DRONE_ID`, `NODE_TYPE`, `ROLE`) farklılaştırılarak çalıştırılır.

**Bridge ağı (custom bridge)** — Docker'ın container'lara sanal bir yerel ağ
kurması. Her container **kendi IP adresini** alır. Alternatif olan "host
networking" modunda tüm container'lar host'un tek IP'sini paylaşırdı; bu da
"drone'lar birbirinin IP'sini ağ üzerinden keşfeder" gereksinimini anlamsız
kılardı. Bu yüzden bilinçli olarak custom bridge seçildi (Bölüm 2).

**Unicast / Multicast** — Unicast, paketin tek bir alıcıya gönderilmesidir.
Multicast ise paketin bir **gruba** (örn. `239.255.0.1`) tek seferde
gönderilmesi; o gruba katılmış tüm dinleyiciler paketi alır. Sürüdeki
heartbeat/telemetri yayınları için idealdir: gönderen, kaç dinleyici olduğunu
bilmek zorunda kalmaz.

**IGMP join** — Bir sürecin çekirdeğe "şu multicast grubunu dinlemek
istiyorum" demesi. Bu yapılmadan multicast paketleri sürece ulaşmaz;
`tools/multicast_check/receiver.py` içinde `IP_ADD_MEMBERSHIP` satırının
yaptığı iş budur.

**TTL (Time To Live)** — Bir IP paketinin kaç yönlendiriciden geçebileceğini
sınırlayan sayaç. Multicast'te yanlışlıkla tüm ağa yayılmayı önler; aynı bridge
ağı içinde `1` yeterlidir.

**RAII (Resource Acquisition Is Initialization)** — C++'ın temel kaynak yönetim
fikri: bir kaynak (bellek, dosya, soket, kilit) bir nesnenin ömrüne bağlanır.
Nesne oluşturulurken kaynak alınır, nesne kapsam (scope) dışına çıkınca
otomatik olarak bırakılır. `std::string`'in belleğini elle serbest bırakmamıza
gerek olmamasının sebebi budur.
