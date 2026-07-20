#include <cstdio>
#include <cuda_runtime.h>

int main() {
    int count = 0;
    cudaError_t err = cudaGetDeviceCount(&count);
    printf("cudaGetDeviceCount -> err=%d (%s), count=%d\n",
           (int)err, cudaGetErrorString(err), count);

    if (count > 0) {
        cudaDeviceProp prop;
        cudaGetDeviceProperties(&prop, 0);
        printf("Device 0: %s, compute capability %d.%d, total mem %.2f GB\n",
               prop.name, prop.major, prop.minor,
               prop.totalGlobalMem / (1024.0 * 1024.0 * 1024.0));

        void* devPtr = nullptr;
        cudaError_t mallocErr = cudaMalloc(&devPtr, 1024);
        printf("cudaMalloc -> err=%d (%s)\n", (int)mallocErr, cudaGetErrorString(mallocErr));
        if (devPtr) cudaFree(devPtr);
    }
    return 0;
}
