#include <hip/hip_runtime.h>
#include <iostream>

template <typename T>
__global__ void addCountReverse(const T* A_d, T* C_d, int64_t NELEM, int count) {
  size_t offset = (blockIdx.x * blockDim.x + threadIdx.x);
  size_t stride = blockDim.x * gridDim.x;

  for (int i = 0; i < count; i++) {
    for (int64_t i = NELEM - stride + offset; i >= 0; i -= stride) {
      C_d[i] = A_d[i] + (T)count;
    }
  }
}

int main() {
    int64_t numElements = 10000000;  // 38.15 MB like in the failing test
    size_t sizeBytes = numElements * sizeof(int);
    
    std::cout << "test: starting sequence with sizeBytes=" << sizeBytes 
              << " bytes, " << sizeBytes / 1024.0 / 1024.0 << " MB\n";

    int *C_h, *C_d;
    hipMalloc(&C_d, sizeBytes);
    hipHostMalloc(&C_h, sizeBytes);

    hipStream_t stream;
    hipStreamCreateWithFlags(&stream, 0x0);

    // Initialize memory
    hipMemset(C_d, 0, sizeBytes);
    for (int i = 0; i < numElements; i++) {
        C_h[i] = -1;
    }

    // Create events
    hipEvent_t start, stop;
    hipEventCreate(&start);
    hipEventCreate(&stop);

    unsigned blocksPerCU = 6;
    unsigned threadsPerBlock = 256;
    unsigned blocks = (numElements + threadsPerBlock - 1) / threadsPerBlock;
    if (blocks > blocksPerCU * 64) blocks = blocksPerCU * 64; // Limit blocks

    std::cout << "test 0x1001: stream=0 waitStart=0 syncMode=syncNone\n";

    // Record start event
    hipEventRecord(start, stream);
    
    // Launch kernel that causes the issue
    hipLaunchKernelGGL(addCountReverse, dim3(blocks), dim3(threadsPerBlock), 
                       0, stream, static_cast<const int*>(C_d), C_h, numElements, 100);
    
    // Record stop event
    hipEventRecord(stop, stream);

    // Don't synchronize (syncNone mode) - this triggers the event dependency issues
    float t;
    hipError_t e = hipEventElapsedTime(&t, start, stop);
    std::cout << "time=" << t << " error=" << hipGetErrorName(e) << std::endl;

    hipEventDestroy(start);
    hipEventDestroy(stop);
    hipStreamDestroy(stream);
    hipFree(C_d);
    hipHostFree(C_h);

    return 0;
} 