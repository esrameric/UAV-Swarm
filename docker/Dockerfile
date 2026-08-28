# =============================================================================
#  Sürü İHA düğümü — çok aşamalı (multi-stage) Docker imajı
#
#  ÇOK AŞAMALI BUILD NEDİR? Derleme için gereken araçlar (derleyici, Java,
#  Fast DDS kaynak kodu, colcon...) yüzlerce megabayt tutar ama ÇALIŞMA anında
#  hiçbirine ihtiyaç yoktur. İki ayrı aşama kullanıp yalnızca üretilen ikili
#  dosyayı ve gereken kütüphaneleri ikinci aşamaya kopyalıyoruz; nihai imaj
#  bu sayede küçük ve saldırı yüzeyi dar kalıyor.
#
#  TEK IMAJ, DÖRT CONTAINER (Bölüm 2): aynı imaj GCS ve üç drone olarak
#  çalıştırılır; farkı ortam değişkenleri belirler.
#
#  Derlemek için (depo kökünden):
#      docker build -f docker/Dockerfile -t swarm_node:latest .
# =============================================================================

# -----------------------------------------------------------------------------
#  1. AŞAMA — builder: Fast DDS'i kaynaktan derler, sonra projeyi derler
# -----------------------------------------------------------------------------
FROM ubuntu:22.04 AS builder

# Geliştirme makinesiyle birebir aynı sürüm (Bölüm 5).
SHELL ["/bin/bash", "-c"]

# DEBIAN_FRONTEND=noninteractive: apt kurulum sırasında soru sormasın
# (Docker build'de cevap verecek kimse yok).
ARG DEBIAN_FRONTEND=noninteractive

# --no-install-recommends: yalnızca gerçekten gereken paketler kurulsun.
# Aynı RUN içinde apt listelerini silmek, katman boyutunu küçük tutar.
RUN apt-get update && apt-get install -y --no-install-recommends \
        build-essential \
        cmake \
        git \
        ca-certificates \
        python3 \
        python3-pip \
        libasio-dev \
        libtinyxml2-dev \
        libssl-dev \
        openjdk-17-jdk-headless \
        libgtest-dev \
    && rm -rf /var/lib/apt/lists/*

# --- Kurumsal TLS-inspection sertifikalari (opsiyonel) -----------------------
#
# Kurumsal aglar HTTPS trafigini denetleyen bir proxy arkasinda olabilir; bu
# durumda disariya yapilan TLS baglantilari kurumun kok sertifikasiyla
# imzali gorunur. Container temiz bir sistem oldugu icin bunu tanimaz.
#
# Ozellikle JAVA KENDI AYRI TRUSTSTORE'UNU kullanir: sistem sertifikalari
# kurulsa bile Java'ya ayrica tanitilmasi gerekir. fastddsgen bir Java
# uygulamasi oldugu ve Gradle'i indirdigi icin, sertifika eksikse imaj
# derlemesi "unable to find valid certification path" hatasiyla durur.
#
# Dizin bostaysa (kurumsal proxy yoksa) bu adim hicbir sey yapmaz.
COPY docker/ca-certificates/ /usr/local/share/ca-certificates/
RUN update-ca-certificates \
    && for sertifika in /usr/local/share/ca-certificates/*.crt; do \
           [ -e "$sertifika" ] || continue; \
           keytool -importcert -noprompt -cacerts -storepass changeit \
                   -alias "$(basename "$sertifika" .crt)" \
                   -file "$sertifika" >/dev/null 2>&1 || true; \
       done

RUN python3 -m pip install --no-cache-dir -U colcon-common-extensions vcstool

# --- Fast DDS (pinlenmiş sürümler) -------------------------------------------
#
# Yalnızca fastdds.repos kopyalanıyor: proje kaynağı değişince bu katmanın
# yeniden derlenmesi gerekmesin diye. Docker katmanları sırayla önbelleklenir;
# ağır Fast DDS derlemesini kaynak kodun ÜSTÜNDE tutmak, sonraki build'leri
# dakikalar yerine saniyeler seviyesine indirir.
WORKDIR /fastdds
COPY tools/fastdds.repos /fastdds/fastdds.repos

# --shallow: depoların geçmişi çekilmez, yalnızca pinlenmiş sürüm alınır.
# Fast-DDS deposu geçmişiyle birlikte yüzlerce megabayt.
RUN mkdir -p src && vcs import --shallow src < fastdds.repos

RUN MAKEFLAGS="-j$(nproc)" colcon build \
        --packages-up-to fastdds \
        --cmake-args -DCMAKE_BUILD_TYPE=Release -DCOMPILE_EXAMPLES=OFF -DBUILD_TESTING=OFF

# fastddsgen bir Gradle projesidir; colcon'un gradle paketleri için task
# extension'ı yok, eProsima'nın resmi yöntemi kendi wrapper'ıyla derlemek.
RUN cd /fastdds/src/fastddsgen && ./gradlew assemble

# --- Proje --------------------------------------------------------------------
WORKDIR /src
COPY CMakeLists.txt ./
COPY idl ./idl
COPY include ./include
COPY src ./src
COPY tests ./tests

RUN source /fastdds/install/setup.bash \
    && export PATH="/fastdds/src/fastddsgen/scripts:$PATH" \
    && cmake -B build -S . -DCMAKE_BUILD_TYPE=Release \
    && cmake --build build -j"$(nproc)"

# Testler imaj derlenirken çalışır: bozuk bir imaj hiç üretilmesin.
RUN source /fastdds/install/setup.bash \
    && ctest --test-dir build --output-on-failure

# -----------------------------------------------------------------------------
#  2. AŞAMA — runtime: yalnızca çalıştırmak için gerekenler
# -----------------------------------------------------------------------------
FROM ubuntu:22.04 AS runtime

ARG DEBIAN_FRONTEND=noninteractive

# Fast DDS'in çalışma anı bağımlılıkları. Derleyici, Java, kaynak kod YOK.
RUN apt-get update && apt-get install -y --no-install-recommends \
        libtinyxml2-9 \
        libssl3 \
    && rm -rf /var/lib/apt/lists/*

# Fast DDS paylaşımlı kütüphaneleri (libfastdds.so, libfastcdr.so).
COPY --from=builder /fastdds/install/fastdds/lib /opt/fastdds/lib
COPY --from=builder /fastdds/install/fastcdr/lib /opt/fastcdr/lib

COPY --from=builder /src/build/swarm_node /usr/local/bin/swarm_node

ENV LD_LIBRARY_PATH=/opt/fastdds/lib:/opt/fastcdr/lib

# Root olarak çalıştırmaya gerek yok; en az yetki ilkesi.
RUN useradd --create-home --shell /usr/sbin/nologin swarm
USER swarm

# Varsayılanlar; docker-compose bunları düğüm başına ezecek (Bölüm 4).
ENV NODE_TYPE=DRONE \
    DRONE_ID=0 \
    ROLE=SCOUT \
    ROS_DOMAIN_ID=42

ENTRYPOINT ["/usr/local/bin/swarm_node"]
