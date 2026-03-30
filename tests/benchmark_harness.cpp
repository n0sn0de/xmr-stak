/**
 * benchmark_harness.cpp — CryptoNight-GPU Performance Benchmark
 *
 * Controlled, repeatable hashrate measurement tool for performance optimization.
 * Runs for a fixed duration, measures hashrate, and ALWAYS terminates cleanly.
 *
 * Usage:
 *   ./benchmark_harness --opencl [--duration 60] [--device 0]
 *   ./benchmark_harness --cuda [--duration 60] [--device 0]
 *   ./benchmark_harness --all-devices [--duration 60]
 *
 * Build (from repo root):
 *   # OpenCL only
 *   g++ -std=c++17 -O2 -march=native -I. tests/benchmark_harness.cpp \
 *       -o tests/benchmark_harness -lOpenCL -lpthread
 *
 *   # With CUDA support (requires nvcc)
 *   nvcc -std=c++17 -O2 -I. tests/benchmark_harness.cpp \
 *        -o tests/benchmark_harness -lOpenCL -lpthread
 *
 * Design Goals:
 * - Reproducible measurements (fixed test vectors, controlled timing)
 * - Clean shutdown (no hangs, no GPU memory leaks)
 * - Single-threaded (isolate GPU performance from thread scheduling)
 * - Minimal overhead (direct GPU kernel dispatch, no pool connection)
 * - Export results (JSON output for tracking across builds)
 */

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <chrono>
#include <atomic>
#include <signal.h>
#include <unistd.h>

// Signal handler for clean shutdown
static std::atomic<bool> g_stop_requested{false};
static void signal_handler(int) {
    g_stop_requested.store(true);
    fprintf(stderr, "\n[BENCHMARK] Stop requested, finishing current iteration...\n");
}

// ============================================================
// Benchmark Configuration
// ============================================================
struct BenchmarkConfig {
    const char* backend;        // "opencl" or "cuda"
    int device_id;              // GPU device index
    int duration_sec;           // How long to run (seconds)
    bool test_all_devices;      // Benchmark all available GPUs
    bool json_output;           // Export JSON results
    const char* output_file;    // Where to write JSON (nullptr = stdout)
};

// ============================================================
// Result Tracking
// ============================================================
struct BenchmarkResult {
    const char* backend;
    int device_id;
    const char* device_name;
    uint64_t hashes_computed;
    double elapsed_sec;
    double hashrate;            // H/s
    double kernel_time_ms;      // GPU execution time
    double memory_bw_gbps;      // Memory bandwidth estimate
};

// ============================================================
// OpenCL Benchmark Implementation
// ============================================================
#ifdef __APPLE__
#include <OpenCL/opencl.h>
#else
#include <CL/cl.h>
#endif

#include <fstream>
#include <sstream>
#include <string>
#include <vector>

static void check_cl(cl_int err, const char* operation) {
    if (err != CL_SUCCESS) {
        fprintf(stderr, "[OPENCL] ERROR: %s failed with code %d\n", operation, err);
        exit(1);
    }
}

// Helper: Load text file
static std::string load_text_file(const char* path) {
    std::ifstream f(path);
    if (!f.is_open()) {
        fprintf(stderr, "[ERROR] Failed to open file: %s\n", path);
        exit(1);
    }
    std::stringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

// Helper: String replacement
static void string_replace_all(std::string& str, const std::string& from, const std::string& to) {
    size_t pos = 0;
    while ((pos = str.find(from, pos)) != std::string::npos) {
        str.replace(pos, from.length(), to);
        pos += to.length();
    }
}

struct OpenCLDevice {
    cl_platform_id platform;
    cl_device_id device;
    cl_context context;
    cl_command_queue queue;
    cl_program program;
    
    // CN-GPU kernels (4 kernels for cn_gpu algorithm)
    cl_kernel kernel_phase1;     // Keccak + AES key expansion
    cl_kernel kernel_phase2;     // Scratchpad expansion
    cl_kernel kernel_phase3;     // Main memory-hard loop
    cl_kernel kernel_phase4_5;   // Implosion + finalize
    
    // GPU memory buffers
    cl_mem input_buf;            // 128-byte input block
    cl_mem scratchpad;           // 2 MiB scratchpad per thread
    cl_mem states;               // 200-byte state per thread
    cl_mem output_buf;           // Output buffer (0x100 uints)
    
    char device_name[128];
    size_t intensity;            // Number of parallel hashes
    size_t worksize;             // Local work size
    uint32_t nonce;              // Starting nonce
};

static bool init_opencl_device(OpenCLDevice* dev, int device_idx, size_t intensity, size_t worksize) {
    cl_int err;
    cl_uint num_platforms;
    err = clGetPlatformIDs(0, nullptr, &num_platforms);
    if (err != CL_SUCCESS || num_platforms == 0) {
        fprintf(stderr, "[OPENCL] No platforms found\n");
        return false;
    }

    cl_platform_id* platforms = (cl_platform_id*)malloc(sizeof(cl_platform_id) * num_platforms);
    clGetPlatformIDs(num_platforms, platforms, nullptr);

    // Find AMD platform (prefer AMD for benchmark consistency)
    dev->platform = platforms[0];
    for (cl_uint i = 0; i < num_platforms; i++) {
        char vendor[128];
        clGetPlatformInfo(platforms[i], CL_PLATFORM_VENDOR, sizeof(vendor), vendor, nullptr);
        if (strstr(vendor, "Advanced Micro Devices") != nullptr) {
            dev->platform = platforms[i];
            break;
        }
    }
    free(platforms);

    cl_uint num_devices;
    err = clGetDeviceIDs(dev->platform, CL_DEVICE_TYPE_GPU, 0, nullptr, &num_devices);
    if (err != CL_SUCCESS || (cl_uint)device_idx >= num_devices) {
        fprintf(stderr, "[OPENCL] Device %d not found (only %d devices available)\n", device_idx, num_devices);
        return false;
    }

    cl_device_id* devices = (cl_device_id*)malloc(sizeof(cl_device_id) * num_devices);
    clGetDeviceIDs(dev->platform, CL_DEVICE_TYPE_GPU, num_devices, devices, nullptr);
    dev->device = devices[device_idx];
    free(devices);

    clGetDeviceInfo(dev->device, CL_DEVICE_NAME, sizeof(dev->device_name), dev->device_name, nullptr);

    // Check max workgroup size
    size_t max_worksize;
    clGetDeviceInfo(dev->device, CL_DEVICE_MAX_WORK_GROUP_SIZE, sizeof(size_t), &max_worksize, nullptr);
    max_worksize /= 16;  // cn_gpu uses 16 threads per hash
    if (worksize > max_worksize) {
        fprintf(stderr, "[OPENCL] WARNING: worksize %zu > device max %zu, clamping\n", worksize, max_worksize);
        worksize = max_worksize;
    }

    dev->context = clCreateContext(nullptr, 1, &dev->device, nullptr, nullptr, &err);
    check_cl(err, "clCreateContext");

#ifdef CL_VERSION_2_0
    dev->queue = clCreateCommandQueueWithProperties(dev->context, dev->device, nullptr, &err);
#else
    dev->queue = clCreateCommandQueue(dev->context, dev->device, 0, &err);
#endif
    check_cl(err, "clCreateCommandQueue");

    // Store benchmark parameters
    dev->intensity = intensity;
    dev->worksize = worksize;
    dev->nonce = 0;

    // Allocate GPU memory
    constexpr size_t SCRATCHPAD_SIZE = 2 * 1024 * 1024;  // 2 MiB per thread
    constexpr size_t STATE_SIZE = 200;                    // State buffer per thread

    // Match production: allocate with rawIntensity (not rounded)
    size_t scratchpad_bytes = SCRATCHPAD_SIZE * intensity;
    size_t states_bytes = STATE_SIZE * intensity;
    
    // But compute rounded g_thd for dispatch (compatibility mode)
    size_t g_thd = ((intensity + worksize - 1) / worksize) * worksize;
    
    fprintf(stderr, "[OPENCL] Allocating buffers: scratchpad=%zu bytes, states=%zu bytes\n",
            scratchpad_bytes, states_bytes);
    fprintf(stderr, "[OPENCL] intensity=%zu, g_thd=%zu, worksize=%zu\n",
            intensity, g_thd, worksize);
    
    // Match production: plain CL_MEM_READ_WRITE (no ALLOC_HOST_PTR)
    dev->input_buf = clCreateBuffer(dev->context, CL_MEM_READ_ONLY, 128, nullptr, &err);
    check_cl(err, "clCreateBuffer(input)");
    fprintf(stderr, "[OPENCL] Created input buffer: ptr=%p\n", (void*)dev->input_buf);

    dev->scratchpad = clCreateBuffer(dev->context, CL_MEM_READ_WRITE, scratchpad_bytes, nullptr, &err);
    check_cl(err, "clCreateBuffer(scratchpad)");
    fprintf(stderr, "[OPENCL] Created scratchpad buffer: ptr=%p, size=%zu MB\n",
            (void*)dev->scratchpad, scratchpad_bytes / (1024*1024));

    dev->states = clCreateBuffer(dev->context, CL_MEM_READ_WRITE, states_bytes, nullptr, &err);
    check_cl(err, "clCreateBuffer(states)");
    fprintf(stderr, "[OPENCL] Created states buffer: ptr=%p, size=%zu bytes\n",
            (void*)dev->states, states_bytes);

    dev->output_buf = clCreateBuffer(dev->context, CL_MEM_READ_WRITE, sizeof(cl_uint) * 0x100, nullptr, &err);
    check_cl(err, "clCreateBuffer(output)");
    fprintf(stderr, "[OPENCL] Created output buffer: ptr=%p\n", (void*)dev->output_buf);

    fprintf(stderr, "[OPENCL] Device %d: %s (intensity=%zu, worksize=%zu, g_thd=%zu)\n", 
            device_idx, dev->device_name, intensity, worksize, g_thd);
    fprintf(stderr, "[OPENCL] Buffer sizes: scratchpad=%zu MB (%.1f MB/hash), states=%zu bytes (%zu bytes/hash)\n",
            scratchpad_bytes / (1024*1024), (double)SCRATCHPAD_SIZE / (1024*1024), 
            states_bytes, STATE_SIZE);
    return true;
}

// Load precompiled binary from production miner's cache
static bool load_cached_binary(OpenCLDevice* dev) {
    cl_int err;
    
    // Production miner's cached binary (known working on this hardware)
    const char* cache_path = "/home/nitro/.openclcache/baf5608de33f91d22d0975bd8b068b9f62878386a60416e87336e9d2c0bf1669.openclbin";
    
    // Load binary file
    std::ifstream bin_file(cache_path, std::ios::binary);
    if (!bin_file.good()) {
        fprintf(stderr, "[OPENCL] Cached binary not found: %s\n", cache_path);
        return false;
    }
    
    bin_file.seekg(0, std::ios::end);
    size_t bin_size = bin_file.tellg();
    bin_file.seekg(0, std::ios::beg);
    
    std::vector<unsigned char> binary(bin_size);
    bin_file.read(reinterpret_cast<char*>(binary.data()), bin_size);
    bin_file.close();
    
    fprintf(stderr, "[OPENCL] Loaded cached binary: %zu bytes from %s\n", bin_size, cache_path);
    
    // Create program from binary
    const unsigned char* bin_ptr = binary.data();
    cl_int bin_status = CL_SUCCESS;
    dev->program = clCreateProgramWithBinary(dev->context, 1, &dev->device, &bin_size, &bin_ptr, &bin_status, &err);
    check_cl(err, "clCreateProgramWithBinary");
    check_cl(bin_status, "clCreateProgramWithBinary (binary status)");
    
    // Build (no options needed — binary is pre-compiled)
    err = clBuildProgram(dev->program, 1, &dev->device, nullptr, nullptr, nullptr);
    if (err != CL_SUCCESS) {
        fprintf(stderr, "[OPENCL] Binary build failed (code %d)\n", err);
        size_t log_size;
        clGetProgramBuildInfo(dev->program, dev->device, CL_PROGRAM_BUILD_LOG, 0, nullptr, &log_size);
        std::vector<char> log(log_size);
        clGetProgramBuildInfo(dev->program, dev->device, CL_PROGRAM_BUILD_LOG, log_size, log.data(), nullptr);
        fprintf(stderr, "%s\n", log.data());
        return false;
    }
    
    fprintf(stderr, "[OPENCL] Using production's cached binary (worksize=8, intensity compatible)\n");
    
    // Extract kernel handles
    dev->kernel_phase1 = clCreateKernel(dev->program, "cn_gpu_phase1_keccak", &err);
    check_cl(err, "clCreateKernel(cn_gpu_phase1_keccak)");
    
    dev->kernel_phase3 = clCreateKernel(dev->program, "cn_gpu_phase3_compute", &err);
    check_cl(err, "clCreateKernel(cn_gpu_phase3_compute)");
    
    dev->kernel_phase4_5 = clCreateKernel(dev->program, "cn_gpu_phase4_finalize", &err);
    check_cl(err, "clCreateKernel(cn_gpu_phase4_finalize)");
    
    dev->kernel_phase2 = clCreateKernel(dev->program, "cn_gpu_phase2_expand", &err);
    check_cl(err, "clCreateKernel(cn_gpu_phase2_expand)");
    
    fprintf(stderr, "[OPENCL] Kernels extracted from cached binary\n");
    return true;
}

// Load and compile CN-GPU OpenCL kernels from source
static bool compile_kernels(OpenCLDevice* dev) {
    cl_int err;
    
    // Load kernel sources (same pattern as production code: #include embeds R"===(...)===")
    const char* cryptonightCL =
#include "../n0s/backend/amd/amd_gpu/opencl/cryptonight.cl"
        ;
    const char* wolfAesCL =
#include "../n0s/backend/amd/amd_gpu/opencl/wolf-aes.cl"
        ;
    const char* cryptonight_gpu =
#include "../n0s/backend/amd/amd_gpu/opencl/cryptonight_gpu.cl"
        ;
    
    // Inline includes (same pattern as production code)
    std::string source_code(cryptonightCL);
    string_replace_all(source_code, "N0S_INCLUDE_WOLF_AES", wolfAesCL);
    string_replace_all(source_code, "N0S_INCLUDE_CN_GPU", cryptonight_gpu);
    
    // Compile program
    const char* source_ptr = source_code.c_str();
    size_t source_len = source_code.length();
    dev->program = clCreateProgramWithSource(dev->context, 1, &source_ptr, &source_len, &err);
    check_cl(err, "clCreateProgramWithSource");
    
    // Build with algorithm constants (cn_gpu hardcoded values)
    constexpr size_t CN_MEMORY = 2 * 1024 * 1024;  // 2 MiB
    constexpr size_t CN_ITER = 262144;             // 256K iterations
    constexpr size_t MASK = CN_MEMORY - 16;        // Thread memory mask
    
    // CRITICAL: Must compile with the SAME worksize used at dispatch time!
    std::string options;
    options += " -DITERATIONS=" + std::to_string(CN_ITER);
    options += " -DMASK=" + std::to_string(MASK) + "U";
    options += " -DWORKSIZE=" + std::to_string(dev->worksize) + "U";  // Device-specific worksize!
    options += " -DCOMP_MODE=1";  // Compatibility mode
    options += " -DMEMORY=" + std::to_string(CN_MEMORY) + "LU";
    options += " -DALGO=5";       // cn_gpu algorithm ID
    options += " -DCN_UNROLL=2";  // Loop unroll factor
    options += " -DOPENCL_DRIVER_MAJOR=14";
    options += " -DIS_WINDOWS_OS=0";
    options += " -cl-fp32-correctly-rounded-divide-sqrt";  // IEEE 754 compliance
    
    fprintf(stderr, "[OPENCL] Compiling from source with WORKSIZE=%zu\n", dev->worksize);
    
    err = clBuildProgram(dev->program, 1, &dev->device, options.c_str(), nullptr, nullptr);
    
    if (err != CL_SUCCESS) {
        fprintf(stderr, "[OPENCL] Kernel compilation failed (code %d)\n", err);
        size_t log_size;
        clGetProgramBuildInfo(dev->program, dev->device, CL_PROGRAM_BUILD_LOG, 0, nullptr, &log_size);
        std::vector<char> log(log_size);
        clGetProgramBuildInfo(dev->program, dev->device, CL_PROGRAM_BUILD_LOG, log_size, log.data(), nullptr);
        fprintf(stderr, "%s\n", log.data());
        return false;
    }
    
    fprintf(stderr, "[OPENCL] Kernels compiled successfully from source\n");
    
    // Extract kernel handles — CRITICAL: array indices don't match phase numbers!
    // Production code mapping: Kernels[0]=phase1, [1]=phase3, [2]=phase4+5, [3]=phase2
    dev->kernel_phase1 = clCreateKernel(dev->program, "cn_gpu_phase1_keccak", &err);
    check_cl(err, "clCreateKernel(cn_gpu_phase1_keccak)");
    
    dev->kernel_phase3 = clCreateKernel(dev->program, "cn_gpu_phase3_compute", &err);
    check_cl(err, "clCreateKernel(cn_gpu_phase3_compute)");
    
    dev->kernel_phase4_5 = clCreateKernel(dev->program, "cn_gpu_phase4_finalize", &err);
    check_cl(err, "clCreateKernel(cn_gpu_phase4_finalize)");
    
    dev->kernel_phase2 = clCreateKernel(dev->program, "cn_gpu_phase2_expand", &err);
    check_cl(err, "clCreateKernel(cn_gpu_phase2_expand)");
    
    fprintf(stderr, "[OPENCL] Kernels extracted successfully (phase1, phase3, phase4+5, phase2)\n");
    return true;
}

static void cleanup_opencl_device(OpenCLDevice* dev) {
    if (dev->output_buf) clReleaseMemObject(dev->output_buf);
    if (dev->states) clReleaseMemObject(dev->states);
    if (dev->scratchpad) clReleaseMemObject(dev->scratchpad);
    if (dev->input_buf) clReleaseMemObject(dev->input_buf);
    if (dev->kernel_phase4_5) clReleaseKernel(dev->kernel_phase4_5);
    if (dev->kernel_phase3) clReleaseKernel(dev->kernel_phase3);
    if (dev->kernel_phase2) clReleaseKernel(dev->kernel_phase2);
    if (dev->kernel_phase1) clReleaseKernel(dev->kernel_phase1);
    if (dev->program) clReleaseProgram(dev->program);
    if (dev->queue) clReleaseCommandQueue(dev->queue);
    if (dev->context) clReleaseContext(dev->context);
}

static BenchmarkResult benchmark_opencl(int device_id, int duration_sec) {
    // Benchmark configuration
    constexpr size_t INTENSITY = 1;     // Number of parallel hashes
    constexpr size_t WORKSIZE = 8;      // Local work size (must be >=8 for Phase1's 8x8 local size)
    
    OpenCLDevice dev = {0};
    if (!init_opencl_device(&dev, device_id, INTENSITY, WORKSIZE)) {
        fprintf(stderr, "[BENCHMARK] Failed to initialize OpenCL device %d\n", device_id);
        exit(1);
    }
    
    // DEBUGGING: Force source compilation to test printf hypothesis
    // bool kernel_load_ok = load_cached_binary(&dev);
    // if (!kernel_load_ok) {
    //     fprintf(stderr, "[OPENCL] Cached binary failed, compiling from source...\n");
    //     kernel_load_ok = compile_kernels(&dev);
    // }
    bool kernel_load_ok = compile_kernels(&dev);  // Force fresh compile with printf
    
    if (!kernel_load_ok) {
        fprintf(stderr, "[BENCHMARK] Failed to load/compile kernels\n");
        exit(1);
    }
    
    // Test vector: 76-byte zero block (from cn_gpu_harness.cpp)
    uint8_t test_input[128] = {0};
    test_input[76] = 0x01;  // Padding byte
    
    // Upload input block
    cl_int err = clEnqueueWriteBuffer(dev.queue, dev.input_buf, CL_TRUE, 0, 128, test_input, 0, nullptr, nullptr);
    check_cl(err, "clEnqueueWriteBuffer(input)");
    
    // Set kernel arguments (declared here for scope visibility in benchmark loop)
    const cl_uint num_threads = (cl_uint)INTENSITY;
    const cl_ulong target = 0xFFFFFFFFFFFFFFFFULL;  // Always accept (benchmark mode)
    
    // Phase 1 kernel: Keccak + AES key expansion
    clSetKernelArg(dev.kernel_phase1, 0, sizeof(cl_mem), &dev.input_buf);
    clSetKernelArg(dev.kernel_phase1, 1, sizeof(cl_mem), &dev.scratchpad);
    clSetKernelArg(dev.kernel_phase1, 2, sizeof(cl_mem), &dev.states);
    clSetKernelArg(dev.kernel_phase1, 3, sizeof(cl_uint), &num_threads);
    
    // Phase 2 kernel: Scratchpad expansion
    clSetKernelArg(dev.kernel_phase2, 0, sizeof(cl_mem), &dev.scratchpad);
    clSetKernelArg(dev.kernel_phase2, 1, sizeof(cl_mem), &dev.states);
    
    // Phase 3 kernel: Main memory-hard loop
    clSetKernelArg(dev.kernel_phase3, 0, sizeof(cl_mem), &dev.scratchpad);
    clSetKernelArg(dev.kernel_phase3, 1, sizeof(cl_mem), &dev.states);
    clSetKernelArg(dev.kernel_phase3, 2, sizeof(cl_uint), &num_threads);
    
    // Phase 4+5 kernel: Implosion + finalize
    clSetKernelArg(dev.kernel_phase4_5, 0, sizeof(cl_mem), &dev.scratchpad);
    clSetKernelArg(dev.kernel_phase4_5, 1, sizeof(cl_mem), &dev.states);
    clSetKernelArg(dev.kernel_phase4_5, 2, sizeof(cl_mem), &dev.output_buf);
    clSetKernelArg(dev.kernel_phase4_5, 3, sizeof(cl_ulong), &target);
    clSetKernelArg(dev.kernel_phase4_5, 4, sizeof(cl_uint), &num_threads);
    
    uint64_t hashes = 0;
    auto t_start = std::chrono::steady_clock::now();
    auto t_end = t_start + std::chrono::seconds(duration_sec);

    fprintf(stderr, "[BENCHMARK] Running CN-GPU benchmark on device %d for %d seconds...\n", device_id, duration_sec);

    // Compute work sizes (must be compatible with kernel requirements)
    size_t g_intensity = INTENSITY;
    size_t w_size = dev.worksize;  // Use actual device worksize (may be clamped)
    
    // Round up g_intensity to multiple of w_size (compatibility mode)
    size_t g_thd = ((g_intensity + w_size - 1) / w_size) * w_size;
    
    fprintf(stderr, "[BENCHMARK] Work sizes: intensity=%zu, worksize=%zu, g_thd=%zu\n", 
            g_intensity, w_size, g_thd);
    fprintf(stderr, "[BENCHMARK] Phase3: global=%zu, local=%zu\n", g_thd * 16, w_size * 16);
    
    // Main benchmark loop
    while (std::chrono::steady_clock::now() < t_end && !g_stop_requested.load()) {
        // Zero output buffer
        cl_uint zero = 0;
        clEnqueueWriteBuffer(dev.queue, dev.output_buf, CL_FALSE, sizeof(cl_uint) * 0xFF, sizeof(cl_uint), &zero, 0, nullptr, nullptr);
        
        // Phase 1: Keccak + AES (2D dispatch)
        size_t nonce_offset[2] = {dev.nonce, 1};
        size_t global_1[2] = {g_thd, 8};
        size_t local_1[2] = {8, 8};
        err = clEnqueueNDRangeKernel(dev.queue, dev.kernel_phase1, 2, nonce_offset, global_1, local_1, 0, nullptr, nullptr);
        check_cl(err, "clEnqueueNDRangeKernel(phase1)");
        err = clFinish(dev.queue);
        if (err != CL_SUCCESS) { fprintf(stderr, "[ERROR] Phase 1 failed: %d\n", err); break; }
        
        // Phase 2: Scratchpad expansion (1D dispatch, 64 threads per hash)
        size_t global_2 = g_intensity * 64;
        size_t local_2 = 64;
        err = clEnqueueNDRangeKernel(dev.queue, dev.kernel_phase2, 1, 0, &global_2, &local_2, 0, nullptr, nullptr);
        check_cl(err, "clEnqueueNDRangeKernel(phase2)");
        err = clFinish(dev.queue);
        if (err != CL_SUCCESS) { fprintf(stderr, "[ERROR] Phase 2 failed: %d\n", err); break; }
        
        // Phase 3: Main loop (1D dispatch, MUST use g_thd * 16 and w_size * 16)
        size_t global_3 = g_thd * 16;
        size_t local_3 = w_size * 16;
        fprintf(stderr, "[DEBUG] Dispatching Phase 3: global=%zu, local=%zu, numThreads=%u\n", global_3, local_3, num_threads);
        err = clEnqueueNDRangeKernel(dev.queue, dev.kernel_phase3, 1, 0, &global_3, &local_3, 0, nullptr, nullptr);
        check_cl(err, "clEnqueueNDRangeKernel(phase3)");
        err = clFinish(dev.queue);
        if (err != CL_SUCCESS) { fprintf(stderr, "[ERROR] Phase 3 failed: %d\n", err); break; }
        
        // Phase 4+5: Finalize (2D dispatch)
        size_t nonce_offset_45[2] = {0, dev.nonce};
        size_t global_45[2] = {8, g_thd};
        size_t local_45[2] = {8, w_size};
        err = clEnqueueNDRangeKernel(dev.queue, dev.kernel_phase4_5, 2, nonce_offset_45, global_45, local_45, 0, nullptr, nullptr);
        check_cl(err, "clEnqueueNDRangeKernel(phase4_5)");
        err = clFinish(dev.queue);
        if (err != CL_SUCCESS) { fprintf(stderr, "[ERROR] Phase 4+5 failed: %d\n", err); break; }
        
        fprintf(stderr, "[DEBUG] All phases completed successfully\n");
        
        hashes += g_intensity;
        dev.nonce += g_intensity;
    }

    auto t_final = std::chrono::steady_clock::now();
    double elapsed = std::chrono::duration<double>(t_final - t_start).count();

    cleanup_opencl_device(&dev);

    BenchmarkResult result = {0};
    result.backend = "opencl";
    result.device_id = device_id;
    result.device_name = strdup(dev.device_name);
    result.hashes_computed = hashes;
    result.elapsed_sec = elapsed;
    result.hashrate = (double)hashes / elapsed;
    result.kernel_time_ms = 0.0;  // TODO: track via clGetEventProfilingInfo
    result.memory_bw_gbps = 0.0;  // TODO: calculate from scratchpad reads/writes

    return result;
}

// ============================================================
// CUDA Benchmark Implementation (Stub)
// ============================================================
#ifdef __NVCC__
#include <cuda_runtime.h>

static BenchmarkResult benchmark_cuda(int device_id, int duration_sec) {
    fprintf(stderr, "[CUDA] Benchmark not yet implemented\n");
    BenchmarkResult result = {0};
    result.backend = "cuda";
    result.device_id = device_id;
    result.device_name = "CUDA Device (stub)";
    result.hashes_computed = 0;
    result.elapsed_sec = 0;
    result.hashrate = 0;
    return result;
}
#endif

// ============================================================
// Result Reporting
// ============================================================
static void print_result(const BenchmarkResult& r) {
    printf("\n");
    printf("===================================================================\n");
    printf("Backend:         %s\n", r.backend);
    printf("Device:          %d (%s)\n", r.device_id, r.device_name);
    printf("Hashes:          %lu\n", (unsigned long)r.hashes_computed);
    printf("Duration:        %.2f sec\n", r.elapsed_sec);
    printf("Hashrate:        %.2f H/s\n", r.hashrate);
    if (r.kernel_time_ms > 0) {
        printf("Kernel Time:     %.2f ms\n", r.kernel_time_ms);
    }
    if (r.memory_bw_gbps > 0) {
        printf("Memory BW:       %.2f GB/s\n", r.memory_bw_gbps);
    }
    printf("===================================================================\n");
}

static void export_json(const BenchmarkResult& r, const char* path) {
    FILE* f = path ? fopen(path, "w") : stdout;
    if (!f) {
        fprintf(stderr, "[ERROR] Failed to open %s for writing\n", path);
        return;
    }

    fprintf(f, "{\n");
    fprintf(f, "  \"backend\": \"%s\",\n", r.backend);
    fprintf(f, "  \"device_id\": %d,\n", r.device_id);
    fprintf(f, "  \"device_name\": \"%s\",\n", r.device_name);
    fprintf(f, "  \"hashes_computed\": %lu,\n", (unsigned long)r.hashes_computed);
    fprintf(f, "  \"elapsed_sec\": %.3f,\n", r.elapsed_sec);
    fprintf(f, "  \"hashrate\": %.2f,\n", r.hashrate);
    fprintf(f, "  \"kernel_time_ms\": %.2f,\n", r.kernel_time_ms);
    fprintf(f, "  \"memory_bw_gbps\": %.2f\n", r.memory_bw_gbps);
    fprintf(f, "}\n");

    if (path) fclose(f);
}

// ============================================================
// Main
// ============================================================
static void print_usage(const char* prog) {
    fprintf(stderr, "Usage:\n");
    fprintf(stderr, "  %s --opencl [--device N] [--duration SEC] [--json output.json]\n", prog);
#ifdef __NVCC__
    fprintf(stderr, "  %s --cuda [--device N] [--duration SEC]\n", prog);
#endif
    fprintf(stderr, "  %s --all-devices [--duration SEC]\n", prog);
    fprintf(stderr, "\n");
    fprintf(stderr, "Options:\n");
    fprintf(stderr, "  --device N         GPU device index (default: 0)\n");
    fprintf(stderr, "  --duration SEC     Benchmark duration in seconds (default: 60)\n");
    fprintf(stderr, "  --json FILE        Export results as JSON\n");
    fprintf(stderr, "\n");
    fprintf(stderr, "Examples:\n");
    fprintf(stderr, "  %s --opencl --device 0 --duration 120\n", prog);
    fprintf(stderr, "  %s --all-devices --duration 30 --json results.json\n", prog);
}

int main(int argc, char** argv) {
    BenchmarkConfig cfg = {0};
    cfg.backend = nullptr;
    cfg.device_id = 0;
    cfg.duration_sec = 60;
    cfg.test_all_devices = false;
    cfg.json_output = false;
    cfg.output_file = nullptr;

    // Parse args
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--opencl") == 0) {
            cfg.backend = "opencl";
        }
#ifdef __NVCC__
        else if (strcmp(argv[i], "--cuda") == 0) {
            cfg.backend = "cuda";
        }
#endif
        else if (strcmp(argv[i], "--all-devices") == 0) {
            cfg.test_all_devices = true;
        }
        else if (strcmp(argv[i], "--device") == 0 && i + 1 < argc) {
            cfg.device_id = atoi(argv[++i]);
        }
        else if (strcmp(argv[i], "--duration") == 0 && i + 1 < argc) {
            cfg.duration_sec = atoi(argv[++i]);
        }
        else if (strcmp(argv[i], "--json") == 0 && i + 1 < argc) {
            cfg.json_output = true;
            cfg.output_file = argv[++i];
        }
        else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            print_usage(argv[0]);
            return 0;
        }
        else {
            fprintf(stderr, "[ERROR] Unknown option: %s\n", argv[i]);
            print_usage(argv[0]);
            return 1;
        }
    }

    if (!cfg.backend && !cfg.test_all_devices) {
        fprintf(stderr, "[ERROR] Must specify --opencl, --cuda, or --all-devices\n");
        print_usage(argv[0]);
        return 1;
    }

    // Install signal handler for clean shutdown
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    printf("=== CryptoNight-GPU Benchmark Harness ===\n");
    printf("Backend:  %s\n", cfg.backend ? cfg.backend : "all");
    printf("Device:   %d\n", cfg.device_id);
    printf("Duration: %d sec\n", cfg.duration_sec);
    printf("\n");

    BenchmarkResult result = {0};

    if (cfg.test_all_devices) {
        fprintf(stderr, "[TODO] Multi-device benchmark not yet implemented\n");
        return 1;
    }

    if (strcmp(cfg.backend, "opencl") == 0) {
        result = benchmark_opencl(cfg.device_id, cfg.duration_sec);
    }
#ifdef __NVCC__
    else if (strcmp(cfg.backend, "cuda") == 0) {
        result = benchmark_cuda(cfg.device_id, cfg.duration_sec);
    }
#endif
    else {
        fprintf(stderr, "[ERROR] Unknown backend: %s\n", cfg.backend);
        return 1;
    }

    print_result(result);

    if (cfg.json_output) {
        export_json(result, cfg.output_file);
        if (cfg.output_file) {
            fprintf(stderr, "\n[BENCHMARK] Results exported to %s\n", cfg.output_file);
        }
    }

    return 0;
}
