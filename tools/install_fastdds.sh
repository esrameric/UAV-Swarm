#!/usr/bin/env bash
#
# Faz 0.1 — Fast DDS'i kaynaktan derleyip kullanıcı-yerel bir workspace'e kurar.
#
# Ubuntu 22.04 (jammy) apt depolarında hazır bir `libfastdds-dev` paketi YOKTUR,
# bu yüzden kaynaktan derlemek zorundayız. Derleme sonucu sisteme (/usr) değil,
# ~/Fast-DDS/install altına kurulur; root yetkisi yalnızca baştaki apt
# paketleri için gerekir.
#
# Sürümler tools/fastdds.repos içinde PİNLENMİŞTİR (Fast DDS v3.6.2 hattı).
#
# Kullanım:  bash tools/install_fastdds.sh
# Sonrasında her yeni terminalde:  source tools/fastdds_env.sh

set -euo pipefail

WORKSPACE="$HOME/Fast-DDS"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPOS_FILE="$SCRIPT_DIR/fastdds.repos"

# --- ROS 2 çakışması temizliği ----------------------------------------------
# Bu proje ROS 2 KULLANMIYOR (Bölüm 2), ama geliştirme makinesinde ROS 2 kurulu
# olabilir. CMake, PATH'teki `.../bin` girdilerinin ÜST dizinini de
# find_package ön eki olarak tarar; yani PATH'te /opt/ros/humble/bin varsa
# CMake /opt/ros/humble/share altındaki ROS paketlerini (ament_cmake_test vb.)
# bulur ve Fast DDS derlemesi bozulur. Bu yüzden PATH'i temizliyoruz.
export PATH="$(echo "$PATH" | tr ':' '\n' | grep -v '/opt/ros/' | paste -sd: -)"
unset AMENT_PREFIX_PATH COLCON_PREFIX_PATH CMAKE_PREFIX_PATH ROS_DISTRO ROS_VERSION || true

echo "==> 1/5  Sistem bağımlılıkları kuruluyor (root yetkisi gerekir)"
# fastddsgen bir Java uygulamasıdır -> openjdk-17-jdk zorunlu.
# libasio-dev / libtinyxml2-dev / libssl-dev ise Fast DDS'in C++ bağımlılıkları.
# libgtest-dev bu projenin birim testleri için gerekli (Bölüm 5).
sudo apt-get update
sudo apt-get install -y \
    cmake g++ python3-pip wget git \
    libasio-dev libtinyxml2-dev libssl-dev openjdk-17-jdk libgtest-dev

# colcon: eProsima'nın önerdiği çok-paketli derleme aracı.
# vcstool: `*.repos` dosyasındaki depoları toplu klonlar.
python3 -m pip install -U colcon-common-extensions vcstool

export JAVA_HOME=/usr/lib/jvm/java-17-openjdk-amd64
export PATH="$JAVA_HOME/bin:$PATH"

echo "==> 2/5  Kaynaklar çekiliyor (pinlenmiş sürümler: $REPOS_FILE)"
mkdir -p "$WORKSPACE/src"
cp "$REPOS_FILE" "$WORKSPACE/fastdds.repos"
vcs import "$WORKSPACE/src" < "$WORKSPACE/fastdds.repos"

echo "==> 3/5  Fast CDR + Fast DDS derleniyor (20-40 dakika sürebilir)"
# --packages-up-to fastdds: fastdds ve bağımlılıklarını (fastcdr,
#   foonathan_memory_vendor) derler.
# MAKEFLAGS=-j3: 4 çekirdek / 8 GB RAM'lik bir makinede paralel derleme belleği
#   tüketip OOM'a düşmesin diye bilinçli sınırlama.
# COMPILE_EXAMPLES=OFF / BUILD_TESTING=OFF: kütüphanenin kendi örnek ve
#   testlerini derlemeye gerek yok, derleme süresini ciddi ölçüde kısaltır.
cd "$WORKSPACE"
MAKEFLAGS="-j3" colcon build \
    --packages-up-to fastdds \
    --cmake-args -DCMAKE_BUILD_TYPE=Release -DCOMPILE_EXAMPLES=OFF -DBUILD_TESTING=OFF

echo "==> 4/5  Fast DDS-Gen (IDL derleyici) derleniyor"
# fastddsgen bir Gradle projesidir; colcon'un gradle paketleri için bir
# "task extension"ı yok, bu yüzden eProsima'nın resmi yöntemiyle, projenin
# kendi gradle wrapper'ı (./gradlew) ile derliyoruz.
cd "$WORKSPACE/src/fastddsgen"
./gradlew assemble

echo "==> 5/5  Doğrulama"
# shellcheck disable=SC1091
source "$SCRIPT_DIR/fastdds_env.sh"
fastddsgen -version

cat <<EOF

Kurulum tamam.

Her yeni terminalde Fast DDS'i ortama almak için:

    source $SCRIPT_DIR/fastdds_env.sh

EOF
