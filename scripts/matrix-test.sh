#!/bin/bash
# Full build matrix test: all Ubuntu LTS × CUDA versions × AMD OpenCL
# Tests that the miner COMPILES (not mines — no GPU in containers)
#
# Usage: ./scripts/matrix-test.sh [--quick] [--filter PATTERN] [--test]
#   --quick   Only test one CUDA + one AMD combo
#   --filter  Only run tests matching PATTERN (e.g. "11.8" or "noble" or "opencl")
#   --test    After compile matrix, deploy and mine-test on available hardware

set -uo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
REPO_DIR="$(dirname "$SCRIPT_DIR")"
MODE="${1:-full}"
FILTER="${2:-}"
DO_MINE_TEST=false

# Parse args — handle --test anywhere in args
for arg in "$@"; do
    case "$arg" in
        --test) DO_MINE_TEST=true ;;
    esac
done

# Colors
RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[0;33m'; CYAN='\033[0;36m'; NC='\033[0m'

PASS=0; FAIL=0; SKIP=0
declare -a RESULTS=()

########################################################################
# Build matrix definition
# Format: "label|image|cmake_method|cuda_archs_space_sep"
########################################################################

NVIDIA_MATRIX=(
    # CUDA 11.8 — Pascal→Ada (GTX 1000 → RTX 4000)
    "cuda11.8-bionic|docker.io/nvidia/cuda:11.8.0-devel-ubuntu18.04|pip|61 75 80 86 89"
    "cuda11.8-focal|docker.io/nvidia/cuda:11.8.0-devel-ubuntu20.04|pip|61 75 80 86 89"
    "cuda11.8-jammy|docker.io/nvidia/cuda:11.8.0-devel-ubuntu22.04|apt|61 75 80 86 89"
    # CUDA 12.6 — Pascal→Hopper
    "cuda12.6-jammy|docker.io/nvidia/cuda:12.6.0-devel-ubuntu22.04|apt|61 75 80 86 89 90"
    "cuda12.6-noble|docker.io/nvidia/cuda:12.6.0-devel-ubuntu24.04|apt|61 75 80 86 89 90"
    # CUDA 12.8 — Pascal→Blackwell (GTX 1000 → RTX 5000)
    "cuda12.8-jammy|docker.io/nvidia/cuda:12.8.1-devel-ubuntu22.04|apt|61 75 80 86 89 90 100 120"
    "cuda12.8-noble|docker.io/nvidia/cuda:12.8.1-devel-ubuntu24.04|apt|61 75 80 86 89 90 100 120"
)

AMD_MATRIX=(
    "opencl-focal|docker.io/ubuntu:20.04|pip|"
    "opencl-jammy|docker.io/ubuntu:22.04|apt|"
    "opencl-noble|docker.io/ubuntu:24.04|apt|"
)

if [ "$MODE" = "--quick" ]; then
    NVIDIA_MATRIX=("cuda11.8-jammy|docker.io/nvidia/cuda:11.8.0-devel-ubuntu22.04|apt|61 75")
    AMD_MATRIX=("opencl-jammy|docker.io/ubuntu:22.04|apt|")
    FILTER="${2:-}"
fi

if [ "$MODE" = "--filter" ]; then
    FILTER="${2:-}"
fi

########################################################################
run_build_test() {
    local LABEL="$1" IMAGE="$2" CMAKE_METHOD="$3" ARCH_OR_EXTRA="$4" IS_CUDA="$5"

    if [ -n "$FILTER" ] && [[ "$LABEL" != *"$FILTER"* ]]; then
        printf "${YELLOW}SKIP${NC} %-25s (filtered)\n" "$LABEL"
        SKIP=$((SKIP + 1)); RESULTS+=("SKIP $LABEL"); return 0
    fi

    printf "${CYAN}TEST${NC} %-25s " "$LABEL"

    # Create a temporary build script for the container
    local BUILDSCRIPT
    BUILDSCRIPT=$(mktemp /tmp/n0s-build-XXXX.sh)

    cat > "$BUILDSCRIPT" << 'INNER_EOF'
#!/bin/bash
set -e
export DEBIAN_FRONTEND=noninteractive
INNER_EOF

    if [ "$CMAKE_METHOD" = "pip" ]; then
        cat >> "$BUILDSCRIPT" << 'INNER_EOF'
apt-get update -qq >/dev/null 2>&1
apt-get install -y -qq software-properties-common wget gpg g++ libmicrohttpd-dev libssl-dev libhwloc-dev >/dev/null 2>&1
# Kitware APT repo for modern cmake on older Ubuntu
wget -qO- https://apt.kitware.com/keys/kitware-archive-latest.asc 2>/dev/null | gpg --dearmor - > /usr/share/keyrings/kitware-archive-keyring.gpg 2>/dev/null
. /etc/os-release
echo "deb [signed-by=/usr/share/keyrings/kitware-archive-keyring.gpg] https://apt.kitware.com/ubuntu/ ${UBUNTU_CODENAME} main" > /etc/apt/sources.list.d/kitware.list
apt-get update -qq >/dev/null 2>&1
apt-get install -y -qq cmake >/dev/null 2>&1
INNER_EOF
    else
        cat >> "$BUILDSCRIPT" << 'INNER_EOF'
apt-get update -qq >/dev/null 2>&1
apt-get install -y -qq cmake g++ libmicrohttpd-dev libssl-dev libhwloc-dev >/dev/null 2>&1
INNER_EOF
    fi

    if [ "$IS_CUDA" = "false" ]; then
        cat >> "$BUILDSCRIPT" << 'INNER_EOF'
apt-get install -y -qq ocl-icd-opencl-dev >/dev/null 2>&1
INNER_EOF
        echo 'CMAKE_EXTRA="-DCUDA_ENABLE=OFF -DOpenCL_ENABLE=ON"' >> "$BUILDSCRIPT"
    else
        local ARCH_SEMI
        ARCH_SEMI=$(echo "$ARCH_OR_EXTRA" | tr ' ' ';')
        echo "CMAKE_EXTRA=\"-DCUDA_ENABLE=ON -DOpenCL_ENABLE=OFF -DCUDA_ARCH='${ARCH_SEMI}'\"" >> "$BUILDSCRIPT"
    fi

    cat >> "$BUILDSCRIPT" << 'INNER_EOF'
cp -r /src /tmp/build
cd /tmp/build && rm -rf build && mkdir build && cd build
eval cmake .. $CMAKE_EXTRA -DMICROHTTPD_ENABLE=ON -DCMAKE_BUILD_TYPE=Release -DN0S_COMPILE=generic >/dev/null 2>&1
cmake --build . -j$(nproc) 2>&1 | tail -5
echo BUILD_SUCCESS
INNER_EOF

    local BUILD_LOG
    BUILD_LOG=$(podman run --rm \
        -v "${REPO_DIR}:/src:ro" \
        -v "${BUILDSCRIPT}:/run.sh:ro" \
        "${IMAGE}" \
        bash /run.sh 2>&1)

    rm -f "$BUILDSCRIPT"

    if echo "$BUILD_LOG" | grep -q "BUILD_SUCCESS"; then
        printf "${GREEN}PASS${NC}\n"
        PASS=$((PASS + 1)); RESULTS+=("PASS $LABEL")
    else
        printf "${RED}FAIL${NC}\n"
        FAIL=$((FAIL + 1)); RESULTS+=("FAIL $LABEL")
        echo "--- Build log (last 20 lines) ---"
        echo "$BUILD_LOG" | tail -20
        echo "---"
    fi
}

########################################################################
echo "═══════════════════════════════════════════════════════════════"
echo "  n0s-ryo-miner Full Build Matrix Test"
echo "═══════════════════════════════════════════════════════════════"
echo "  NVIDIA: ${#NVIDIA_MATRIX[@]} configs  |  AMD: ${#AMD_MATRIX[@]} configs"
echo ""

echo "── NVIDIA CUDA Builds ──────────────────────────────────────"
for entry in "${NVIDIA_MATRIX[@]}"; do
    IFS='|' read -r LABEL IMAGE CMAKE_METHOD ARCHS <<< "$entry"
    run_build_test "$LABEL" "$IMAGE" "$CMAKE_METHOD" "$ARCHS" "true"
done

echo ""
echo "── AMD OpenCL Builds ───────────────────────────────────────"
for entry in "${AMD_MATRIX[@]}"; do
    IFS='|' read -r LABEL IMAGE CMAKE_METHOD EXTRA <<< "$entry"
    run_build_test "$LABEL" "$IMAGE" "$CMAKE_METHOD" "$EXTRA" "false"
done

echo ""
echo "═══════════════════════════════════════════════════════════════"
printf "  Results: ${GREEN}%d passed${NC}, ${RED}%d failed${NC}, ${YELLOW}%d skipped${NC}\n" "$PASS" "$FAIL" "$SKIP"
echo "═══════════════════════════════════════════════════════════════"
echo ""
for r in "${RESULTS[@]}"; do
    S=$(echo "$r" | cut -d' ' -f1); L=$(echo "$r" | cut -d' ' -f2-)
    case "$S" in
        PASS) printf "  ${GREEN}✅${NC} %s\n" "$L" ;;
        FAIL) printf "  ${RED}❌${NC} %s\n" "$L" ;;
        SKIP) printf "  ${YELLOW}⏭️${NC}  %s\n" "$L" ;;
    esac
done
echo ""

########################################################################
# Optional: Mine test on real hardware using container-built binaries
########################################################################
if [ "$DO_MINE_TEST" = true ]; then
    echo ""
    echo "═══════════════════════════════════════════════════════════════"
    echo "  Hardware Mine Tests (--test)"
    echo "═══════════════════════════════════════════════════════════════"
    echo ""

    MINE_PASS=0; MINE_FAIL=0

    # Build artifacts first (container-build.sh produces dist/cuda-X.Y/)
    for CUDA_VER in 11.8 12.6; do
        DIST="${REPO_DIR}/dist/cuda-${CUDA_VER}"
        if [ ! -f "${DIST}/n0s-ryo-miner" ]; then
            echo "Building CUDA ${CUDA_VER} artifacts for mine test..."
            "${SCRIPT_DIR}/container-build.sh" "${CUDA_VER}" "" "22.04" || true
        fi
    done

    # CUDA 11.8 → nos2 (Pascal, GTX 1070 Ti)
    if [ -f "${REPO_DIR}/dist/cuda-11.8/n0s-ryo-miner" ]; then
        printf "${CYAN}MINE${NC} %-25s " "cuda11.8 → nos2 (Pascal)"
        if "${SCRIPT_DIR}/test-remote-binary.sh" nos2 "${REPO_DIR}/dist/cuda-11.8" 45 >/dev/null 2>&1; then
            printf "${GREEN}PASS${NC}\n"; MINE_PASS=$((MINE_PASS + 1))
        else
            printf "${RED}FAIL${NC}\n"; MINE_FAIL=$((MINE_FAIL + 1))
        fi
    fi

    # CUDA 12.6 → nosnode (Turing, RTX 2070)
    if [ -f "${REPO_DIR}/dist/cuda-12.6/n0s-ryo-miner" ]; then
        printf "${CYAN}MINE${NC} %-25s " "cuda12.6 → nosnode (Turing)"
        if "${SCRIPT_DIR}/test-remote-binary.sh" nosnode "${REPO_DIR}/dist/cuda-12.6" 50 >/dev/null 2>&1; then
            printf "${GREEN}PASS${NC}\n"; MINE_PASS=$((MINE_PASS + 1))
        else
            printf "${RED}FAIL${NC}\n"; MINE_FAIL=$((MINE_FAIL + 1))
        fi
    fi

    # AMD OpenCL → local (RX 9070 XT)
    printf "${CYAN}MINE${NC} %-25s " "opencl → nitro (RX 9070 XT)"
    if (cd "${REPO_DIR}" && ./test-mine.sh >/dev/null 2>&1); then
        printf "${GREEN}PASS${NC}\n"; MINE_PASS=$((MINE_PASS + 1))
    else
        printf "${RED}FAIL${NC}\n"; MINE_FAIL=$((MINE_FAIL + 1))
    fi

    echo ""
    echo "═══════════════════════════════════════════════════════════════"
    printf "  Mine Tests: ${GREEN}%d passed${NC}, ${RED}%d failed${NC}\n" "$MINE_PASS" "$MINE_FAIL"
    echo "═══════════════════════════════════════════════════════════════"
fi

[ $FAIL -eq 0 ]
