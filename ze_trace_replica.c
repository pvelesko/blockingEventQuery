#include <level_zero/ze_api.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/time.h>
#include <signal.h>

#define CHECK_ZE_RESULT(result, msg) \
    if (result != ZE_RESULT_SUCCESS) { \
        printf("ERROR: %s failed with result %d\n", msg, result); \
        exit(1); \
    }

// Timeout handler
void timeout_handler(int sig) {
    printf("TIMEOUT: zeEventQueryStatus took too long, terminating...\n");
    exit(1);
}

long get_time_us() {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return tv.tv_sec * 1000000 + tv.tv_usec;
}

int main() {
    ze_result_t result;
    
    // Set up timeout - kill after 5 seconds
    signal(SIGALRM, timeout_handler);
    alarm(5);
    
    printf("ze_trace_replica: starting sequence with sizeBytes=40000000 bytes, 38.147 MB\n");
    
    // Initialize Level Zero
    result = zeInit(0);
    CHECK_ZE_RESULT(result, "zeInit");
    
    // Get driver
    uint32_t driverCount = 0;
    result = zeDriverGet(&driverCount, NULL);
    CHECK_ZE_RESULT(result, "zeDriverGet count");
    
    if (driverCount == 0) {
        printf("ERROR: No Level Zero drivers found\n");
        exit(1);
    }
    
    ze_driver_handle_t hDriver;
    result = zeDriverGet(&driverCount, &hDriver);
    CHECK_ZE_RESULT(result, "zeDriverGet");
    
    // Get device
    uint32_t deviceCount = 0;
    result = zeDeviceGet(hDriver, &deviceCount, NULL);
    CHECK_ZE_RESULT(result, "zeDeviceGet count");
    
    if (deviceCount == 0) {
        printf("ERROR: No Level Zero devices found\n");
        exit(1);
    }
    
    ze_device_handle_t hDevice;
    uint32_t getDeviceCount = 1;  // We only want to get the first device
    result = zeDeviceGet(hDriver, &getDeviceCount, &hDevice);
    CHECK_ZE_RESULT(result, "zeDeviceGet");
    
    // Create context
    ze_context_desc_t contextDesc = {
        .stype = ZE_STRUCTURE_TYPE_CONTEXT_DESC,
        .pNext = NULL,
        .flags = 0
    };
    ze_context_handle_t hContext;
    result = zeContextCreateEx(hDriver, &contextDesc, 1, &hDevice, &hContext);
    CHECK_ZE_RESULT(result, "zeContextCreateEx");
    
    // Allocate memory - device memory (40MB like in trace)
    ze_device_mem_alloc_desc_t deviceDesc = {
        .stype = ZE_STRUCTURE_TYPE_DEVICE_MEM_ALLOC_DESC,
        .pNext = NULL,
        .flags = ZE_DEVICE_MEM_ALLOC_FLAG_BIAS_CACHED,
        .ordinal = 0
    };
    void* devicePtr;
    result = zeMemAllocDevice(hContext, &deviceDesc, 40000000, 0, hDevice, &devicePtr);
    CHECK_ZE_RESULT(result, "zeMemAllocDevice");
    
    // Allocate host memory
    ze_host_mem_alloc_desc_t hostDesc = {
        .stype = ZE_STRUCTURE_TYPE_HOST_MEM_ALLOC_DESC,
        .pNext = NULL,
        .flags = ZE_HOST_MEM_ALLOC_FLAG_BIAS_CACHED
    };
    void* hostPtr;
    result = zeMemAllocHost(hContext, &hostDesc, 40000000, 4096, &hostPtr);
    CHECK_ZE_RESULT(result, "zeMemAllocHost");
    
    // Allocate shared memory for timestamps
    void* sharedPtr1;
    result = zeMemAllocShared(hContext, &deviceDesc, &hostDesc, 32, 8, NULL, &sharedPtr1);
    CHECK_ZE_RESULT(result, "zeMemAllocShared 1");
    
    void* sharedPtr2;
    result = zeMemAllocShared(hContext, &deviceDesc, &hostDesc, 32, 8, NULL, &sharedPtr2);
    CHECK_ZE_RESULT(result, "zeMemAllocShared 2");
    
    // Create command queue
    ze_command_queue_desc_t cmdQueueDesc = {
        .stype = ZE_STRUCTURE_TYPE_COMMAND_QUEUE_DESC,
        .pNext = NULL,
        .ordinal = 0,
        .index = 0,
        .flags = 0,
        .mode = ZE_COMMAND_QUEUE_MODE_ASYNCHRONOUS,
        .priority = ZE_COMMAND_QUEUE_PRIORITY_NORMAL
    };
    ze_command_queue_handle_t hCmdQueue1, hCmdQueue2;
    result = zeCommandQueueCreate(hContext, hDevice, &cmdQueueDesc, &hCmdQueue1);
    CHECK_ZE_RESULT(result, "zeCommandQueueCreate 1");
    result = zeCommandQueueCreate(hContext, hDevice, &cmdQueueDesc, &hCmdQueue2);
    CHECK_ZE_RESULT(result, "zeCommandQueueCreate 2");
    
    // Create immediate command lists
    ze_command_list_handle_t hCmdList1, hCmdList2;
    result = zeCommandListCreateImmediate(hContext, hDevice, &cmdQueueDesc, &hCmdList1);
    CHECK_ZE_RESULT(result, "zeCommandListCreateImmediate 1");
    result = zeCommandListCreateImmediate(hContext, hDevice, &cmdQueueDesc, &hCmdList2);
    CHECK_ZE_RESULT(result, "zeCommandListCreateImmediate 2");
    
    // Create event pool
    ze_event_pool_desc_t eventPoolDesc = {
        .stype = ZE_STRUCTURE_TYPE_EVENT_POOL_DESC,
        .pNext = NULL,
        .flags = ZE_EVENT_POOL_FLAG_HOST_VISIBLE,
        .count = 1000
    };
    ze_event_pool_handle_t hEventPool;
    result = zeEventPoolCreate(hContext, &eventPoolDesc, 0, NULL, &hEventPool);
    CHECK_ZE_RESULT(result, "zeEventPoolCreate");
    
    // Create events
    ze_event_desc_t eventDesc = {
        .stype = ZE_STRUCTURE_TYPE_EVENT_DESC,
        .pNext = NULL,
        .index = 0,
        .signal = ZE_EVENT_SCOPE_FLAG_HOST,
        .wait = ZE_EVENT_SCOPE_FLAG_HOST
    };
    
    ze_event_handle_t events[12];  // Need one more event to avoid reuse
    for (int i = 0; i < 12; i++) {
        eventDesc.index = i;
        result = zeEventCreate(hEventPool, &eventDesc, &events[i]);
        CHECK_ZE_RESULT(result, "zeEventCreate");
        
        result = zeEventHostReset(events[i]);
        CHECK_ZE_RESULT(result, "zeEventHostReset");
    }
    
    printf("test 0x1001: stream=0 waitStart=0 syncMode=syncNone\n");
    
    // Memory fill operation (like hipMemset)
    uint8_t pattern = 0;
    result = zeCommandListAppendMemoryFill(hCmdList1, devicePtr, &pattern, 1, 40000000, events[0], 0, NULL);
    CHECK_ZE_RESULT(result, "zeCommandListAppendMemoryFill");
    
    // Synchronize memfill
    result = zeEventHostSynchronize(events[0], UINT64_MAX);
    CHECK_ZE_RESULT(result, "zeEventHostSynchronize memfill");
    
    result = zeCommandQueueSynchronize(hCmdQueue1, UINT64_MAX);
    CHECK_ZE_RESULT(result, "zeCommandQueueSynchronize 1");
    
    // Load SPIR-V kernel
    FILE* fp = fopen("kernel.spv", "rb");
    if (!fp) {
        printf("ERROR: Cannot open kernel.spv\n");
        exit(1);
    }
    
    fseek(fp, 0, SEEK_END);
    size_t kernelSize = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    
    uint8_t* kernelData = malloc(kernelSize);
    fread(kernelData, 1, kernelSize, fp);
    fclose(fp);
    
    // Create module
    ze_module_desc_t moduleDesc = {
        .stype = ZE_STRUCTURE_TYPE_MODULE_DESC,
        .pNext = NULL,
        .format = ZE_MODULE_FORMAT_IL_SPIRV,
        .inputSize = kernelSize,
        .pInputModule = kernelData,
        .pBuildFlags = NULL,
        .pConstants = NULL
    };
    ze_module_handle_t hModule;
    ze_module_build_log_handle_t hBuildLog;
    result = zeModuleCreate(hContext, hDevice, &moduleDesc, &hModule, &hBuildLog);
    CHECK_ZE_RESULT(result, "zeModuleCreate");
    
    // Create kernels
    ze_kernel_desc_t kernelDesc = {
        .stype = ZE_STRUCTURE_TYPE_KERNEL_DESC,
        .pNext = NULL,
        .flags = ZE_KERNEL_FLAG_FORCE_RESIDENCY,
        .pKernelName = "__chip_reset_non_symbols"
    };
    ze_kernel_handle_t hKernelReset;
    result = zeKernelCreate(hModule, &kernelDesc, &hKernelReset);
    CHECK_ZE_RESULT(result, "zeKernelCreate reset");
    
    kernelDesc.pKernelName = "_Z15addCountReverseIiEvPKT_PS0_li";
    ze_kernel_handle_t hKernelMain;
    result = zeKernelCreate(hModule, &kernelDesc, &hKernelMain);
    CHECK_ZE_RESULT(result, "zeKernelCreate main");
    
    // Get device timestamps (start of timing sequence)
    uint64_t hostTimestamp1, deviceTimestamp1;
    result = zeDeviceGetGlobalTimestamps(hDevice, &hostTimestamp1, &deviceTimestamp1);
    CHECK_ZE_RESULT(result, "zeDeviceGetGlobalTimestamps 1");
    
    // Write global timestamp
    result = zeCommandListAppendWriteGlobalTimestamp(hCmdList2, sharedPtr2, events[3], 0, NULL);
    CHECK_ZE_RESULT(result, "zeCommandListAppendWriteGlobalTimestamp 1");
    
    // Copy timestamp
    uint64_t* timestampDst1 = malloc(8);
    result = zeCommandListAppendMemoryCopy(hCmdList2, timestampDst1, sharedPtr2, 8, events[4], 1, &events[3]);
    CHECK_ZE_RESULT(result, "zeCommandListAppendMemoryCopy 1");
    
    // Barrier 1
    result = zeCommandListAppendBarrier(hCmdList2, events[1], 1, &events[4]);
    CHECK_ZE_RESULT(result, "zeCommandListAppendBarrier 1");
    
    // Barrier 2 
    result = zeCommandListAppendBarrier(hCmdList2, events[5], 1, &events[4]);
    CHECK_ZE_RESULT(result, "zeCommandListAppendBarrier 2");
    
    // Launch reset kernel first
    result = zeKernelSetGroupSize(hKernelReset, 1, 1, 1);
    CHECK_ZE_RESULT(result, "zeKernelSetGroupSize reset");
    
    result = zeKernelSetIndirectAccess(hKernelReset, ZE_KERNEL_INDIRECT_ACCESS_FLAG_HOST | ZE_KERNEL_INDIRECT_ACCESS_FLAG_DEVICE);
    CHECK_ZE_RESULT(result, "zeKernelSetIndirectAccess reset");
    
    ze_group_count_t launchArgs = {1, 1, 1};
    result = zeCommandListAppendLaunchKernel(hCmdList1, hKernelReset, &launchArgs, events[6], 1, &events[5]);
    CHECK_ZE_RESULT(result, "zeCommandListAppendLaunchKernel reset");
    
    // Synchronize reset kernel
    result = zeEventHostSynchronize(events[6], UINT64_MAX);
    CHECK_ZE_RESULT(result, "zeEventHostSynchronize reset");
    
    result = zeCommandQueueSynchronize(hCmdQueue1, UINT64_MAX);
    CHECK_ZE_RESULT(result, "zeCommandQueueSynchronize 2");
    
    // Setup main kernel arguments
    result = zeKernelSetArgumentValue(hKernelMain, 0, sizeof(void*), &devicePtr);
    CHECK_ZE_RESULT(result, "zeKernelSetArgumentValue 0");
    
    result = zeKernelSetArgumentValue(hKernelMain, 1, sizeof(void*), &hostPtr);
    CHECK_ZE_RESULT(result, "zeKernelSetArgumentValue 1");
    
    int64_t numElements = 10000000;
    result = zeKernelSetArgumentValue(hKernelMain, 2, sizeof(int64_t), &numElements);
    CHECK_ZE_RESULT(result, "zeKernelSetArgumentValue 2");
    
    int count = 100;
    result = zeKernelSetArgumentValue(hKernelMain, 3, sizeof(int), &count);
    CHECK_ZE_RESULT(result, "zeKernelSetArgumentValue 3");
    
    // Launch main kernel
    result = zeKernelSetGroupSize(hKernelMain, 256, 1, 1);
    CHECK_ZE_RESULT(result, "zeKernelSetGroupSize main");
    
    result = zeKernelSetIndirectAccess(hKernelMain, ZE_KERNEL_INDIRECT_ACCESS_FLAG_HOST | ZE_KERNEL_INDIRECT_ACCESS_FLAG_DEVICE);
    CHECK_ZE_RESULT(result, "zeKernelSetIndirectAccess main");
    
    launchArgs.groupCountX = 384;  // 384 blocks like in trace
    result = zeCommandListAppendLaunchKernel(hCmdList2, hKernelMain, &launchArgs, events[7], 1, &events[5]);
    CHECK_ZE_RESULT(result, "zeCommandListAppendLaunchKernel main");
    
    // Get second timestamp
    uint64_t hostTimestamp2, deviceTimestamp2;
    result = zeDeviceGetGlobalTimestamps(hDevice, &hostTimestamp2, &deviceTimestamp2);
    CHECK_ZE_RESULT(result, "zeDeviceGetGlobalTimestamps 2");
    
    // Write second global timestamp
    result = zeCommandListAppendWriteGlobalTimestamp(hCmdList2, sharedPtr2, events[8], 1, &events[7]);
    CHECK_ZE_RESULT(result, "zeCommandListAppendWriteGlobalTimestamp 2");
    
    // Copy second timestamp
    uint64_t* timestampDst2 = malloc(8);
    result = zeCommandListAppendMemoryCopy(hCmdList2, timestampDst2, sharedPtr2, 8, events[9], 1, &events[8]);
    CHECK_ZE_RESULT(result, "zeCommandListAppendMemoryCopy 2");
    
    // Final barriers - FIXED: Use separate events to avoid circular dependency
    result = zeCommandListAppendBarrier(hCmdList2, events[11], 1, &events[9]);  // Use events[11] instead of events[1]
    CHECK_ZE_RESULT(result, "zeCommandListAppendBarrier final 1");
    
    result = zeCommandListAppendBarrier(hCmdList2, events[10], 1, &events[9]);
    CHECK_ZE_RESULT(result, "zeCommandListAppendBarrier final 2");
    
    // THIS IS THE CRITICAL CALL - zeEventQueryStatus on the barrier event
    printf("About to call zeEventQueryStatus - this should complete quickly now...\n");
    long start_time = get_time_us();
    
    result = zeEventQueryStatus(events[1]);  // Query the original barrier event (should be fast now)
    printf("zeEventQueryStatus result: %d\n", result);
    
    long end_time = get_time_us();
    long elapsed_us = end_time - start_time;
    
    if (elapsed_us > 100) {
        printf("CHIP critical: zeEventQueryStatus took %ld microseconds (>100 microseconds threshold)\n", elapsed_us);
    } else {
        printf("zeEventQueryStatus completed in %ld microseconds\n", elapsed_us);
    }
    
    CHECK_ZE_RESULT(result, "zeEventQueryStatus");
    
    // Cleanup
    free(kernelData);
    free(timestampDst1);
    free(timestampDst2);
    
    for (int i = 0; i < 12; i++) {
        zeEventDestroy(events[i]);
    }
    zeEventPoolDestroy(hEventPool);
    zeKernelDestroy(hKernelReset);
    zeKernelDestroy(hKernelMain);
    zeModuleDestroy(hModule);
    zeCommandListDestroy(hCmdList1);
    zeCommandListDestroy(hCmdList2);
    zeCommandQueueDestroy(hCmdQueue1);
    zeCommandQueueDestroy(hCmdQueue2);
    zeMemFree(hContext, devicePtr);
    zeMemFree(hContext, hostPtr);
    zeMemFree(hContext, sharedPtr1);
    zeMemFree(hContext, sharedPtr2);
    zeContextDestroy(hContext);
    
    printf("Reproducer completed successfully\n");
    return 0;
}