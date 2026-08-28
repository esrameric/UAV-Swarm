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

Fast DDS ortamda olmalı (`source tools/fastdds_env.sh`), aksi halde CMake
`find_package(fastdds)` adımında durur.

```bash
source tools/fastdds_env.sh
cmake -B build -S .
cmake --build build -j3
ctest --test-dir build --output-on-failure
```

IDL dosyaları derleme sırasında `fastddsgen` ile otomatik olarak C++'a
çevrilir (`build/generated/` altına). Bir `.idl` dosyasını değiştirince
sonraki derlemede kod kendiliğinden yeniden üretilir.

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
| V9 | ENU orijini | Orijin `(0,0,0)` = **GCS'in başlangıç konumu**; `x` = Doğu, `y` = Kuzey, `z` = Yukarı, birim **metre** | Bölüm 9 bu noktayı açık bırakmıştı. GCS sabit ve sürünün referans noktası olduğu için doğal orijin; SITL'de gerçek GPS gerektirmez. |
| V10 | Telemetri `timestamp` birimi | Unix epoch'tan beri **milisaniye** (`unsigned long long`) | Bölüm 3.5'e göre bu alan yalnızca bilgi/log amaçlı; bayatlık kararı `seq_num` ile verilir. Milisaniye log okunabilirliği için yeterli çözünürlük. |
| V11 | `task_id` ve `transaction_id` tipi | `unsigned long` (uint32_t) | Planda tip belirtilmemişti; `seq_num` ile aynı genişlikte tutuldu, tutarlılık ve taşma payı için fazlasıyla yeterli. |
| V12 | `DroneState` | Düğümün kendi konum/hız/batarya durumu için ayrı bir struct eklendi (`include/swarm/drone_state.hpp`) | Planda adı geçmiyordu ama hareket task'ları ve telemetri yayını için zorunlu. `PeerInfo` "başkalarını nasıl görüyorum", `DroneState` "ben neredeyim" bilgisidir. |
| V13 | Uçuş sabitleri | Yatay hız **5 m/s**, iniş hızı **1 m/s**, varış toleransı **0,5 m**, tarama süresi **5 sn**, FailSafe değerlendirme süresi **1 sn** | Gerçek bir uçuş kontrolcüsü yok (SITL); hareket sabit hızla düz çizgi olarak modellendi. Değerler okunabilir, isimlendirilmiş sabitler olarak ilgili task başlıklarında. |
| V14 | `FailSafeTask` davranışı | Aracı anında durdurur, kısa bir "değerlendirme" süresi bekler, sonra biter (ardından `LandingTask` işletilir) | Geçici bir ağ kesintisi kalıcı arıza gibi davranıp sürüyü gereksiz yere indirmesin diye önce durup bekliyor. |
| V15 | `DiscoveryTask` bitiş koşulu | En az **bir** peer duyulunca **veya** 5 sn dolunca biter | Bölüm 2'deki non-blocking keşif stratejisi: 3 drone'un hepsi beklenmez, gerçek sahada bir drone hiç ayağa kalkmayabilir. |
| V16 | Oy verecek kimse yoksa | `ConsensusTask` anında `COMMITTED` olur | Tek başına kalmış bir düğüm oylamada sonsuza kadar kilitlenmemeli. |
| V17 | Acil durum tanımı | `check_emergency()` iki koşula bakar: kendi batarya **< %15**, veya **bir zamanlar duyulmuş** bir peer'ın susması | Plan `check_emergency()`'nin varlığını söylüyor ama içeriğini tanımlamıyordu. "Hiç duyulmamış" drone acil durum sayılmaz — Bölüm 2'deki non-blocking keşif stratejisiyle tutarlı olması için. |
| V18 | Acil durum davranışı | Kuyruk boşaltılır, `FailSafeTask` → `LandingTask` yerleştirilir, sonra `IdleTask`'a düşülür | Faz 6.4'ün beklediği davranış: bir container durdurulduğunda `FailSafeTask` tetiklenir. |
| V19 | Yayın kanalları | `SwarmManager` heartbeat/telemetri yayınını `std::function` üzerinden yapar | Faz 3'te FastDDS henüz yok. Bu katman sayesinde SwarmManager DDS'ten bağımsız test edilebiliyor; Faz 4'te `FastDDSWrapper` kanalları dolduruyor. |

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

**`#pragma once`** — Bir başlık dosyasının, aynı derleme biriminde birden
fazla kez `#include` edilse bile yalnızca bir kez işlenmesini sağlar. Aynı
tipin iki kez tanımlanmasından doğan derleme hatalarını önler.

**`struct` vs `class`** — C++'ta ikisi neredeyse aynıdır; tek fark varsayılan
erişim seviyesidir (`struct`'ta `public`, `class`'ta `private`). Gelenek
olarak sadece veri taşıyan tipler için `struct`, davranışı olan tipler için
`class` kullanılır.

**Default member initializer** — Bir üye alanın tanımında doğrudan `= 0` gibi
bir başlangıç değeri vermek. Kurucu metotta ayrıca belirtilmezse alan bu
değeri alır; böylece "ilklenmemiş çöp değer" okuma hatası baştan imkânsızlaşır.

**`const` üye fonksiyon** — İmzanın sonundaki `const` (`bool is_in_swarm()
const`), "bu fonksiyon nesneyi değiştirmez" sözüdür. Derleyici bu sözü zorlar
ve fonksiyonun `const` nesneler üzerinde de çağrılabilmesini sağlar.

**`#include`** — Başka bir dosyanın içeriğini bulunduğu yere kopyalayan
önişlemci (preprocessor) komutu. Açılı parantez `<...>` sistem/kütüphane
başlıkları, çift tırnak `"..."` projenin kendi başlıkları için kullanılır.

**C++ standardı (C++17)** — Dilin sürümüdür. Bu proje C++17 kullanır; derleyici
hangi standartta çalıştığını `__cplusplus` makrosuyla bildirir (C++17 için
`201703L`).

**`static_assert`** — Bir koşulu **derleme zamanında** kontrol eder; koşul
sağlanmazsa program hiç derlenmez. Çalışma zamanına kalmadan hata yakalamanın
en ucuz yoludur.

### Veri Tipleri ve IDL

**IDL (Interface Definition Language)** — Dilden bağımsız bir mesaj tanımlama
dili. Bir `.idl` dosyasında "bu mesajın içinde şu alanlar var" denir;
`fastddsgen` bunu C++ sınıflarına ve DDS'in ihtiyaç duyduğu
serileştirme/deserileştirme koduna çevirir. Böylece mesajın tel formatı (wire
format) tek bir kaynaktan üretilir, elle yazılmaz.

**`enum` / `enum class`** — Sınırlı sayıda isimlendirilmiş seçenekten oluşan
tip. `role`'ü `int` yerine `DroneRole` yapmak, geçersiz değer atamayı derleme
hatasına çevirir ve kodu `role == DroneRole::SCOUT` gibi okunur kılar.
C++'ta `enum class`, düz `enum`'dan iki noktada daha güvenlidir: değerleri
kapsayan tipin adıyla nitelenmek zorundadır (`DroneRole::SCOUT`), ve sessizce
`int`'e dönüşmez. `fastddsgen` IDL enum'larını `enum class ... : int32_t`
olarak üretir.

**Tel formatı (wire format)** — Bir mesajın ağda gerçekten hangi baytlarla
temsil edildiği. Enum'larda taşınan şey ismin kendisi değil sıra numarasıdır;
bu yüzden IDL'deki enum sıralaması keyfi olarak değiştirilemez — değiştirilirse
eski ve yeni sürüm birbirini yanlış anlar.

**Modül / namespace** — IDL'deki `module swarm { ... }`, C++ tarafında
`namespace swarm { ... }` olur. İsimleri bir çatı altında toplayarak farklı
kütüphanelerdeki aynı isimli tiplerin çakışmasını önler: `swarm::Heartbeat`.

### Derleme Kavramları

**Kütüphane (library) hedefi** — Tek başına çalışmayan, başka programların
bağlanıp kullandığı derlenmiş kod paketi. `STATIC` kütüphane, kendisine
bağlanan programın **içine kopyalanır** (çalışırken ayrı bir dosya taşımak
gerekmez); `SHARED` (`.so`) ise çalışma anında ayrıca yüklenir.

**`PUBLIC` / `PRIVATE` bağlama** — CMake'te bir kütüphanenin bağımlılığı
`PUBLIC` ise, o kütüphaneye bağlanan herkes bağımlılığı da otomatik görür;
`PRIVATE` ise bağımlılık yalnızca kütüphanenin kendi içinde kalır. `swarm_msgs`
Fast DDS'e `PUBLIC` bağlanır, çünkü üretilen başlıklar Fast DDS tiplerini
kullanıcının koduna da taşır.

**Kod üretimi (code generation)** — Kaynak kodun elle değil, başka bir
tanımdan otomatik üretilmesi. Burada `.idl` dosyaları derleme sırasında
`fastddsgen` ile C++'a çevrilir. Üretilen kod depoya konmaz; tek doğruluk
kaynağı `.idl` dosyasıdır.

**Serileştirme / deserileştirme** — Bir nesneyi ağda taşınabilecek bayt
dizisine çevirmek (serialize) ve karşı tarafta geri nesneye dönüştürmek
(deserialize). DDS bunu bizim yerimize yapar; `fastddsgen`'in ürettiği
`...PubSubType` sınıfları tam olarak bu işi üstlenir.

### Sınıflar ve Standart Kütüphane

**Kurucu (constructor) ve başlatma listesi** — Kurucu, bir nesne
oluşturulurken çalışan özel fonksiyondur. `: uye_(deger)` şeklindeki
başlatma listesi, üyeleri gövdeye girilmeden önce doğrudan kurar; gövde
içinde atama yapmaktan hem daha verimlidir hem de `const` üyeler için tek
yoldur.

**`explicit`** — Tek parametreli kurucuların istenmeyen **örtük** (implicit)
tip dönüşümü yapmasını engeller. `explicit` olmasaydı, bir fonksiyona
yanlışlıkla süre verildiğinde derleyici onu sessizce `PeerManager`'a
çevirebilirdi.

**`std::map`** — Anahtar–değer çiftlerini anahtara göre **sıralı** tutan kap.
`operator[]` "yoksa ekle, varsa getir" davranışı gösterir; `find()` ise
aramayı yapar ama **kayıt eklemez** — telemetri, henüz tanışmadığımız bir
peer'ı tabloya sokmasın diye bu ayrım önemlidir.

**Iterator ve `end()`** — Kaplarda gezinmeyi sağlayan "imleç". `find()`
aradığını bulamazsa `end()` döner; bu yüzden dönen değeri kullanmadan önce
`!= end()` kontrolü şarttır.

**Range-for (`for (auto& x : kap)`)** — Bir kabın tüm elemanlarını gezmenin
kısa yolu. Baştaki `&` önemlidir: onsuz her eleman **kopyalanır** ve
üzerinde yapılan değişiklik kaptaki asıl kayda yansımaz.

**`nullptr`** — "Hiçbir şeyi göstermeyen işaretçi". `find()` gibi
"bulamayabilirim" diyen fonksiyonlar bunu döndürür; çağıran taraf
kullanmadan önce kontrol etmelidir.

**Sahiplik (ownership)** — Bir kaynağı silmekten kimin sorumlu olduğu.
`PeerManager::find()` ham işaretçi döndürür ama **sahipliği devretmez**:
işaret edilen kayıt `PeerManager`'a aittir, çağıran onu silmemelidir.

**Zamanın dışarıdan verilmesi (dependency injection)** — `PeerManager`
fonksiyonları içeride `steady_clock::now()` çağırmak yerine `now`
parametresi alır. Böylece 3 saniyelik zaman aşımı, testte gerçekten 3 saniye
beklenerek değil, ileri bir zaman değeri verilerek anında ve deterministik
olarak sınanabilir.

### Eşzamanlılık (Concurrency)

**Thread (iş parçacığı)** — Bir program içinde eşzamanlı ilerleyen ayrı bir
çalışma akışı. Bu sistemde üç thread var: ağ dinleme (I/O ağırlıklı) ile
görev yürütme (hesap ağırlıklı) birbirini bekletmesin diye ayrıldılar.

**Singleton** — Bir sınıftan programda **yalnızca bir** nesne olmasını
garanti eden tasarım deseni. Kurucu `private` yapılır, kopyalama/taşıma
`= delete` ile silinir, tek örnek `get_instance()` içinde tutulur. Burada
gerekli, çünkü peer table ve kuyruklar tek bir gerçeğin kaydı olmalı: üç
thread'in aynı tabloyu görmesi şart.

**Meyers Singleton** — Tek örneği fonksiyon içinde `static` yerel değişken
olarak tutma tekniği. C++11'den beri bu tür değişkenlerin ilklenmesi
**thread-safe olmak zorundadır**: iki thread aynı anda `get_instance()`
çağırsa bile nesne yalnızca bir kez kurulur. Elle kilit yazmaktan daha
güvenlidir.

**`= delete`** — "Bu fonksiyon yok; kullanmaya çalışan derleme hatası alsın."
Kopyalamayı silmek, singleton garantisinin kazara kırılmasını **derleme
zamanında** engeller.

**Lambda** — Yerinde tanımlanan isimsiz fonksiyon: `[yakalama](parametreler)
{ gövde }`. Köşeli parantez içindeki **yakalama listesi**, lambda'nın
dışarıdaki hangi değişkenleri kullanacağını söyler; `&` referansla (asıl
nesne), isim tek başına ise kopyayla yakalar.

**Mutex (`std::mutex`)** — "Mutual exclusion" = karşılıklı dışlama. Aynı
anda yalnızca bir thread'in korunan veriye dokunmasını sağlayan kilit.

**Veri yarışı (data race)** — İki thread'in aynı veriye, en az biri yazarak,
kilitsiz erişmesi. C++'ta bu **tanımsız davranıştır**: bazen çalışır, bazen
sessizce yanlış sonuç verir, bazen çöker. Bu yüzden paylaşılan her yapı bir
mutex ile korunur.

**`std::lock_guard`** — RAII tabanlı kilit. Kurulduğu anda mutex'i kilitler,
kapsam bittiğinde **otomatik** açar — fonksiyondan erken `return` edilse veya
istisna atılsa bile. Elle `lock()`/`unlock()` yazmak, bir yolda `unlock`'u
unutup tüm programı kilitleme riski taşır.

**`mutable`** — Bir üyenin, `const` üye fonksiyon içinde bile
değiştirilebilmesini sağlar. Mutex'ler için gerekli: `peer_count()` mantıksal
olarak `const`'tur ama okumak için yine de kilidi kilitlemek zorundadır.

**Neden her veriye mutex konmaz?** `task_queue_`'ya yalnızca Task Engine
thread'i dokunur. Tek thread'in eriştiği veriye kilit koymak hem gereksiz
maliyettir hem de kodu okuyana yanlış bir "burada yarış var" sinyali verir.

**`std::deque`** — İki uçtan da hızlı ekleme/çıkarma yapılabilen kuyruk.
Komutlar sona eklenir, baştan işlenir (FIFO — ilk giren ilk çıkar).

**`std::move` ve taşıma (move)** — `std::unique_ptr` kopyalanamaz (tek
sahiplik kuralı), yalnızca **taşınabilir**. `std::move`, "bu nesnenin
sahipliğini devrediyorum" demenin yoludur; taşımadan sonra çağıranın elindeki
işaretçi boşalır.

**`std::atomic<bool>`** — Birden fazla thread'in kilitsiz, güvenle okuyup
yazabildiği bayrak. Sıradan bir `bool` burada veri yarışı olurdu: derleyici
onu bir yazmaca (register) alıp döngüden çıkarabilir ve thread durma
işaretini hiç görmeyebilirdi.

**`joinable()`** — Bir `std::thread` gerçekten başlatılmış ve henüz `join`
edilmemiş mi? `join` edilmemiş bir thread nesnesi yok edilirse program
`std::terminate` ile aniden sonlanır; bu yüzden `stop()` hepsini tek tek
bekler.

**`join()`** — Bir thread'in bitmesini beklemek. Beklenmezse ana thread önce
bitip programı sonlandırabilir ve iş yarım kalır.

### Dağıtık Sistemler

**Consensus (uzlaşma)** — Birbirinden bağımsız düğümlerin ortak bir karara
varması. Sürüde göreve başlamak toplu bir karardır: bir drone "başladım"
sanırken diğeri beklemede kalırsa sürü tutarsız duruma düşer.

**2-Phase Commit (2PC)** — İki aşamalı uzlaşma protokolü. **1. aşama
(propose):** bir düğüm (burada GCS) "şunu yapalım mı?" diye sorar.
**2. aşama (vote):** herkes `ACK` veya `NACK` ile cevaplar. Karar
**oybirliği** gerektirir: tek bir `NACK` bile işlemi iptal ettirir. Bu yüzden
`ConsensusTask` ilk `NACK`'te diğerlerini beklemeden `ABORTED` olur.

**Zaman aşımı (timeout)** — Cevap gelmeyen bir düğüm için sonsuza kadar
beklenmez. Burada süre **5 saniyedir**; dolduğunda tüm görev iptal edilir ve
sürü `IdleTask`'a döner. Heterojen bir sürüde 1 drone eksikken göreve
başlamak riskli olduğu için davranış bilinçli olarak "net ve öngörülebilir"
seçildi.

**"Cevap yok" ≠ "Hayır"** — Zaman aşımı ile açık `NACK` aynı sonuca
(`ABORTED`) götürür ama **sebepleri farklıdır**; teşhis ve log için ayırt
edilebilir tutulur (`timeout_ile_iptal_oldu()`).

### Nesne Yönelimli Programlama

**Sınıf (`class`) ve kalıtım (inheritance)** — Sınıf, veri ve davranışı bir
arada tanımlayan tiptir. `class SahteGorev : public Task` yazımı kalıtımdır:
"SahteGorev **bir** Task'tır". Child sınıf, taban sınıfın arayüzünü devralır.

**Soyut sınıf ve saf sanal fonksiyon** — Gövdesi olmayan, `= 0` ile biten
fonksiyona **saf sanal** (pure virtual) denir. En az bir tanesine sahip sınıf
**soyut** olur: doğrudan örneği oluşturulamaz, yalnızca miras alınmak için
vardır. Her child, bu fonksiyonların gövdesini yazmak zorundadır.

**Polimorfizm** — Farklı türden nesneleri **aynı arayüz** üzerinden
kullanabilmek. Task Engine elindeki görevin hangi child olduğunu bilmez,
hepsini `Task*` olarak tutar; `run()` çağrısı yine de doğru child'ın
gövdesine gider. Bu sayede yeni bir görev eklemek mevcut hiçbir dosyayı
değiştirmeyi gerektirmez (**Open/Closed ilkesi**).

**State Pattern** — Bir nesnenin davranışının, içinde bulunduğu duruma göre
değişmesini; her durumu ayrı bir sınıf yaparak çözen tasarım deseni. Burada
her görev (Idle, Hover, Landing...) ayrı bir `Task` child'ıdır. Alternatifi
olan dev bir `if/else` veya `switch` bloğu her yeni durumda büyür ve
okunamaz hâle gelir.

**`virtual` yıkıcı** — Polimorfik olarak kullanılan (taban sınıf işaretçisi
üzerinden silinen) her sınıfın yıkıcısı `virtual` olmalıdır. Aksi hâlde
silme anında yalnızca taban sınıfın yıkıcısı çalışır, child'ınki atlanır ve
sessiz bir kaynak sızıntısı oluşur.

**`override`** — "Bu fonksiyon taban sınıftaki sanal bir fonksiyonu eziyor"
demektir. İsim veya imza yanlış yazılırsa derleyici hata verir; sessizce
yepyeni bir fonksiyon tanımlamış olmaktan korur.

**`std::function`** — Bir fonksiyonu **değişkende saklamanın** yolu. Buraya
lambda, serbest fonksiyon veya üye fonksiyon bağlanabilir. `SwarmManager`
yayın kanallarını `std::function` olarak tuttuğu için DDS'i hiç tanımadan
yayın yapabiliyor; kanalları Faz 4'te `FastDDSWrapper` dolduruyor, testlerde
ise bir lambda dolduruyor.

**`dynamic_cast`** — Taban sınıf işaretçisinin gerçekte hangi child'a ait
olduğunu **çalışma zamanında** sorar; aradığımız tip değilse `nullptr` döner.
Task Engine burada kullanır, çünkü "görev bitti" sinyalinin anlamı
`ConsensusTask` için özeldir (iptal mi, başarı mı?). Polimorfizmin doğal
akışını bozduğu için ölçülü kullanılır.

**Akıllı işaretçi (`std::unique_ptr`)** — İşaret ettiği nesnenin tek
sahibidir ve kapsam dışına çıkınca onu **otomatik siler**. `delete` yazmayı
unutma ihtimalini ortadan kaldırır — RAII'nin bellek yönetimine uygulanmış
hâlidir.

### Sürü Durumu

**Peer / peer table** — "Peer", sürüdeki başka bir düğüm demektir. Her düğüm,
duyduğu diğer düğümler için bir `PeerInfo` kaydı tutar; bu kayıtların
tamamına peer table denir. Diske yazılmaz, ağa gönderilmez — tamamen yerel
bir görüntüdür: "ben şu an sürüyü böyle görüyorum".

**Sentinel değer** — Bir alanın "değer yok / geçersiz" hâlini anlatmak için
ayrılan özel değer. `swarm_id` için sentinel **0**'dır. `-1` seçilmedi çünkü
`swarm_id` işaretsiz (`uint8_t`) bir tiptir: `-1` ataması sessizce `255`'e
döner ve sonraki `== -1` karşılaştırmaları C++'ın integer promotion kuralları
yüzünden her zaman `false` verir.

**`steady_clock` vs `system_clock`** — `system_clock` duvar saatidir; NTP
düzeltmesiyle geri gidebilir. `steady_clock` ise monotoniktir, asla geri
gitmez. "Ne kadar süre geçti?" sorusunun doğru aracı `steady_clock`'tur —
heartbeat sessizliğini bu yüzden onunla ölçüyoruz.

**Alıcı-taraflı saat (receiver-side timestamping)** — Bir peer'ın canlılığını,
onun mesaja gömdüğü zaman damgasıyla değil, **bizim paketi aldığımız andaki
kendi saatimizle** ölçmek. Container'ların duvar saatleri senkron olmayabilir;
alıcı-taraflı ölçüm, ağ gecikmesini "sessizlik" ile karıştırma riskini ortadan
kaldırır.

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
