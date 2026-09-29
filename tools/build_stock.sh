#!/bin/bash
# Reproduce the stock KhiCAS CG50 build (CLAUDE.md §9 step 1).
# Run inside WSL2 Ubuntu (or any x86_64 Linux) from the repo root:
#   bash tools/build_stock.sh
# Non-interactive port of Parisse's linux_compile_khicas.sh. Differences:
#   - installs build-essential (mkhelp is built with the host c++)
#   - works in $KHICAS_WORK (default ~/khicas-work, on the Linux filesystem: NTFS
#     breaks casiolocal's symlinks and is slow) and copies results to ./build/
#   - strips the Makefile's /bin/cp lines to /shared/tmp and ~/.wine
#   - `make clean` first: giacbf.tgz ships prebuilt .o/.g3a files, so a bare
#     `make` would not prove anything. Prebuilt outputs are kept in
#     upstream/prebuilt/ for size/hash comparison.
set -euo pipefail
BASE=https://www-fourier.univ-grenoble-alpes.fr/~parisse/casio
REPO=$(pwd)
UP=${KHICAS_WORK:-$HOME/khicas-work}
mkdir -p "$UP"

sudo apt-get update
sudo apt-get install -y build-essential cmake zlib1g-dev libpng-dev wget tar

# libmpfr.so.4 (needed by casiolocal's gcc 5.3)
if [ ! -f /usr/local/lib/libmpfr.so.4 ]; then
  wget -q -O /tmp/libmpfr.so.4 $BASE/libmpfr.so.4
  sudo cp /tmp/libmpfr.so.4 /usr/local/lib/
  grep -qx /usr/local/lib /etc/ld.so.conf || echo /usr/local/lib | sudo tee -a /etc/ld.so.conf
  sudo ldconfig
fi

# casiolocal (sh3eb-elf gcc, ustl, tommath, libc) unpacks into $HOME
if ! [ -x "$HOME/opt/sh3eb-elf/bin/sh3eb-elf-gcc" ]; then
  (cd "$HOME" && wget -q $BASE/casiolocal.tgz && tar xzf casiolocal.tgz && rm casiolocal.tgz)
fi
export PATH="$HOME/opt/sh3eb-elf/bin:$PATH"
sh3eb-elf-gcc --version | head -1

# mkg3a
if ! command -v mkg3a >/dev/null; then
  (cd /tmp && wget -q $BASE/mkg3a.tgz && tar xzf mkg3a.tgz && cd mkg3a/build && cmake -DCMAKE_POLICY_VERSION_MINIMUM=3.5 .. && make && sudo make install)
fi

# libfxcg (Parisse's modified Prizm SDK)
(cd "$UP" && wget -q -N $BASE/libfxcg.tgz && rm -rf libfxcg && tar xzf libfxcg.tgz && cd libfxcg && make install)

# giacbf sources
cd "$UP"
wget -q -N $BASE/giacbf.tgz
rm -rf giacbf && tar xzf giacbf.tgz
cd giacbf
mkdir -p ../prebuilt && cp -f khicas50.g3a khicas50.ac2 emucas50.g3a khicas50.882 ../prebuilt/
sed -i "s#/home/parisse#$HOME#g" Makefile
sed -i '/\/bin\/cp/d' Makefile
make clean
time make khicas50.g3a khicas50.ac2 emucas50.g3a khicas50.882 2>&1 | tee ../build.log

echo "=== outputs"
ls -l khicas50.g3a khicas50.ac2 emucas50.g3a khicas50.882
sha256sum khicas50.g3a khicas50.ac2 emucas50.g3a khicas50.882
echo "=== prebuilt (upstream tarball)"
ls -l ../prebuilt/

# copy results back to the repo (git-ignored build/)
mkdir -p "$REPO/build/prebuilt"
cp -f khicas50.g3a khicas50.ac2 emucas50.g3a khicas50.882 "$REPO/build/"
cp -f ../prebuilt/* "$REPO/build/prebuilt/"
cp -f ../build.log "$REPO/build/build.log"
echo "=== copied to $REPO/build/"
