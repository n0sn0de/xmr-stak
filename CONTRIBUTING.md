# Contributing to n0s-cngpu

Thanks for your interest in contributing! Here's how to get started.

## Building

```bash
# Clone and build (CPU-only, quickest)
git clone https://github.com/n0sn0de/xmr-stak.git n0s-cngpu
cd n0s-cngpu
mkdir build && cd build
cmake .. -DCUDA_ENABLE=OFF -DOpenCL_ENABLE=OFF
make -j$(nproc)
```

See [Compilation Guide](doc/compile/compile_Linux.md) for GPU build instructions.

## Testing

All changes must pass the containerized build tests before merging:

```bash
# Run CPU-only builds across all Ubuntu LTS versions
./scripts/test-all-distros.sh --cpu-only

# Run OpenCL builds
./scripts/test-all-distros.sh --opencl

# Run everything
./scripts/test-all-distros.sh --all
```

Requires podman or docker. If neither is available, at minimum verify a native build:

```bash
cd build && cmake .. -DCUDA_ENABLE=OFF -DOpenCL_ENABLE=OFF && make -j$(nproc)
./bin/n0s-cngpu --help
./bin/n0s-cngpu --version
```

## Pull Requests

1. Fork the repo and create your branch from `master`
2. Make your changes — keep commits focused and well-described
3. Verify your changes build and pass smoke tests
4. Open a PR against `master`
5. Describe what you changed and why

## Code Style

- Follow the existing code style (K&R braces, tabs for indentation in C++)
- Keep changes minimal — don't reformat unrelated code
- Comment non-obvious logic

## Scope

This miner is **CryptoNight-GPU only**. We won't accept PRs that:

- Add support for other algorithms
- Re-introduce developer fees
- Add Windows or macOS build support (Linux-only for now)
- Add unnecessary dependencies

## Reporting Issues

- Check existing issues first
- Include: distro version, compiler version, GPU model (if relevant), and full error output
- Use the build/test scripts to reproduce issues

## License

By contributing, you agree that your contributions will be licensed under GPLv3.
