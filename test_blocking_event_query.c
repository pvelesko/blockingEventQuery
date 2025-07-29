#define _POSIX_C_SOURCE 199309L
#define _DEFAULT_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <level_zero/ze_api.h>

#define CHECK_ZE(call) \
    do { \
        ze_result_t result = call; \
        if (result != ZE_RESULT_SUCCESS) { \
            printf("ZE error at %s:%d: %d\n", __FILE__, __LINE__, result); \
            exit(1); \
        } \
    } while(0)

long get_time_microseconds() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000000L + ts.tv_nsec / 1000L;
}

// Simulate chipStar's kernel binary (dummy data)
static const char dummy_kernel_binary[] = {
    0x7f, 0x45, 0x4c, 0x46, 0x02, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    // ... (simplified kernel binary data)
};

// Match the EXACT trace pattern - simulating chipStar HIP → Level Zero translation
int main() {
    printf("Reproducing chipStar hipEventElapsedTime blocking trace pattern\n");
    
    // === Phase 1: Driver/Device Initialization (lines 7-48) ===
    CHECK_ZE(zeInit(0)); // flags: []
    
    uint32_t driverCount = 0;
    CHECK_ZE(zeDriverGet(&driverCount, NULL));
    
    driverCount = 1;
    ze_driver_handle_t driver;
    CHECK_ZE(zeDriverGet(&driverCount, &driver));
    
    // Get driver extensions (like trace line 13-16)
    uint32_t extensionCount = 0;
    CHECK_ZE(zeDriverGetExtensionProperties(driver, &extensionCount, NULL));
    
    ze_driver_extension_properties_t *extensions = malloc(extensionCount * sizeof(ze_driver_extension_properties_t));
    CHECK_ZE(zeDriverGetExtensionProperties(driver, &extensionCount, extensions));
    free(extensions);
    
    ze_driver_properties_t driverProps = {.stype = ZE_STRUCTURE_TYPE_DRIVER_PROPERTIES};
    CHECK_ZE(zeDriverGetProperties(driver, &driverProps));
    
    uint32_t deviceCount = 0;
    CHECK_ZE(zeDeviceGet(driver, &deviceCount, NULL));
    
    deviceCount = 2; // Match trace - 2 devices
    ze_device_handle_t devices[2];
    CHECK_ZE(zeDeviceGet(driver, &deviceCount, devices));
    
    // Use first device (Arc A770 in trace)
    ze_device_handle_t device = devices[0];
    
    // Create context with both devices (line 23-24)
    ze_context_desc_t contextDesc = {
        .stype = ZE_STRUCTURE_TYPE_CONTEXT_DESC,
        .pNext = NULL,
        .flags = 0
    };
    ze_context_handle_t context;
    CHECK_ZE(zeContextCreateEx(driver, &contextDesc, deviceCount, devices, &context));
    
    // Get device properties (lines 25-42)
    ze_device_properties_t deviceProps = {.stype = ZE_STRUCTURE_TYPE_DEVICE_PROPERTIES};
    CHECK_ZE(zeDeviceGetProperties(device, &deviceProps));
    
    uint32_t queueGroupCount = 0;
    CHECK_ZE(zeDeviceGetCommandQueueGroupProperties(device, &queueGroupCount, NULL));
    
    ze_command_queue_group_properties_t *queueGroupProps = malloc(queueGroupCount * sizeof(ze_command_queue_group_properties_t));
    for (uint32_t i = 0; i < queueGroupCount; i++) {
        queueGroupProps[i].stype = ZE_STRUCTURE_TYPE_COMMAND_QUEUE_GROUP_PROPERTIES;
    }
    CHECK_ZE(zeDeviceGetCommandQueueGroupProperties(device, &queueGroupCount, queueGroupProps));
    free(queueGroupProps);
    
    // More device property queries to match trace
    uint32_t memPropsCount = 1;
    ze_device_memory_properties_t memProps = {.stype = ZE_STRUCTURE_TYPE_DEVICE_MEMORY_PROPERTIES};
    CHECK_ZE(zeDeviceGetMemoryProperties(device, &memPropsCount, &memProps));
    
    ze_device_compute_properties_t computeProps = {.stype = ZE_STRUCTURE_TYPE_DEVICE_COMPUTE_PROPERTIES};
    CHECK_ZE(zeDeviceGetComputeProperties(device, &computeProps));
    
    uint32_t cachePropsCount = 1;
    ze_device_cache_properties_t cacheProps = {.stype = ZE_STRUCTURE_TYPE_DEVICE_CACHE_PROPERTIES};
    CHECK_ZE(zeDeviceGetCacheProperties(device, &cachePropsCount, &cacheProps));
    
    ze_device_module_properties_t moduleProps = {.stype = ZE_STRUCTURE_TYPE_DEVICE_MODULE_PROPERTIES};
    CHECK_ZE(zeDeviceGetModuleProperties(device, &moduleProps));
    
    ze_device_image_properties_t imageProps = {.stype = ZE_STRUCTURE_TYPE_DEVICE_IMAGE_PROPERTIES};
    CHECK_ZE(zeDeviceGetImageProperties(device, &imageProps));
    
    // === Phase 2: Memory Allocation (lines 43-82) ===
    
    // Shared memory for global data (line 43-44)
    ze_device_mem_alloc_desc_t deviceAllocDesc = {
        .stype = ZE_STRUCTURE_TYPE_DEVICE_MEM_ALLOC_DESC,
        .pNext = NULL,
        .flags = ZE_DEVICE_MEM_ALLOC_FLAG_BIAS_CACHED,
        .ordinal = 0
    };
    ze_host_mem_alloc_desc_t hostAllocDesc = {
        .stype = ZE_STRUCTURE_TYPE_HOST_MEM_ALLOC_DESC,
        .pNext = NULL,
        .flags = ZE_HOST_MEM_ALLOC_FLAG_BIAS_CACHED
    };
    void *sharedGlobal;
    CHECK_ZE(zeMemAllocShared(context, &deviceAllocDesc, &hostAllocDesc, 32, 8, NULL, &sharedGlobal));
    
    // Command queue and immediate command list (lines 45-48)
    ze_command_queue_desc_t queueDesc = {
        .stype = ZE_STRUCTURE_TYPE_COMMAND_QUEUE_DESC,
        .pNext = NULL,
        .ordinal = 0,
        .index = 0,
        .flags = 0,
        .mode = ZE_COMMAND_QUEUE_MODE_ASYNCHRONOUS,
        .priority = ZE_COMMAND_QUEUE_PRIORITY_NORMAL
    };
    ze_command_queue_handle_t queue;
    CHECK_ZE(zeCommandQueueCreate(context, device, &queueDesc, &queue));
    
    ze_command_list_handle_t cmdList;
    CHECK_ZE(zeCommandListCreateImmediate(context, device, &queueDesc, &cmdList));
    
    // === Phase 3: hipMalloc → zeMemAllocDevice (lines 68-71) ===
    void *deviceMem;
    CHECK_ZE(zeMemAllocDevice(context, &deviceAllocDesc, 40000000, 0, device, &deviceMem));
    
    // === Phase 4: hipHostMalloc → zeMemAllocHost (lines 72-75) ===
    void *hostMem;
    CHECK_ZE(zeMemAllocHost(context, &hostAllocDesc, 40000000, 4096, &hostMem));
    
    // === Phase 5: hipStreamCreate → more Level Zero setup (lines 76-83) ===
    void *streamSharedMem;
    CHECK_ZE(zeMemAllocShared(context, &deviceAllocDesc, &hostAllocDesc, 32, 8, NULL, &streamSharedMem));
    
    ze_command_queue_handle_t streamQueue;
    CHECK_ZE(zeCommandQueueCreate(context, device, &queueDesc, &streamQueue));
    
    ze_command_list_handle_t streamCmdList;
    CHECK_ZE(zeCommandListCreateImmediate(context, device, &queueDesc, &streamCmdList));
    
    // === Phase 6: hipMemset → zeMemoryFill (lines 84-99) ===
    ze_event_pool_desc_t eventPoolDesc = {
        .stype = ZE_STRUCTURE_TYPE_EVENT_POOL_DESC,
        .pNext = NULL,
        .flags = ZE_EVENT_POOL_FLAG_HOST_VISIBLE,
        .count = 1
    };
    ze_event_pool_handle_t memsetEventPool;
    CHECK_ZE(zeEventPoolCreate(context, &eventPoolDesc, 0, NULL, &memsetEventPool));
    
    ze_event_desc_t eventDesc = {
        .stype = ZE_STRUCTURE_TYPE_EVENT_DESC,
        .pNext = NULL,
        .index = 0,
        .signal = ZE_EVENT_SCOPE_FLAG_HOST,
        .wait = ZE_EVENT_SCOPE_FLAG_HOST
    };
    ze_event_handle_t memsetEvent;
    CHECK_ZE(zeEventCreate(memsetEventPool, &eventDesc, &memsetEvent));
    CHECK_ZE(zeEventHostReset(memsetEvent));
    
    uint8_t pattern = 0;
    CHECK_ZE(zeCommandListAppendMemoryFill(cmdList, deviceMem, &pattern, 1, 40000000, memsetEvent, 0, NULL));
    CHECK_ZE(zeEventHostSynchronize(memsetEvent, UINT64_MAX));
    CHECK_ZE(zeCommandQueueSynchronize(queue, UINT64_MAX));
    
    // === Phase 7: hipEventCreate events (lines 100-123) ===
    ze_event_pool_handle_t eventPools[4];
    ze_event_handle_t events[4];
    
    for (int i = 0; i < 4; i++) {
        ze_event_pool_desc_t poolDesc = {
            .stype = ZE_STRUCTURE_TYPE_EVENT_POOL_DESC,
            .pNext = NULL,
            .flags = ZE_EVENT_POOL_FLAG_HOST_VISIBLE,
            .count = 1
        };
        CHECK_ZE(zeEventPoolCreate(context, &poolDesc, 0, NULL, &eventPools[i]));
        
        ze_event_desc_t evtDesc = {
            .stype = ZE_STRUCTURE_TYPE_EVENT_DESC,
            .pNext = NULL,
            .index = 0,
            .signal = ZE_EVENT_SCOPE_FLAG_HOST,
            .wait = ZE_EVENT_SCOPE_FLAG_HOST
        };
        CHECK_ZE(zeEventCreate(eventPools[i], &evtDesc, &events[i]));
    }
    
    // === Phase 8: hipEventRecord operations (lines 126-197) ===
    CHECK_ZE(zeEventHostReset(events[1])); // hipEventRecord with blocking flag
    
    // Create timing event pool and events
    ze_event_pool_desc_t timingPoolDesc = {
        .stype = ZE_STRUCTURE_TYPE_EVENT_POOL_DESC,
        .pNext = NULL,
        .flags = ZE_EVENT_POOL_FLAG_HOST_VISIBLE,
        .count = 2
    };
    ze_event_pool_handle_t timingPool;
    CHECK_ZE(zeEventPoolCreate(context, &timingPoolDesc, 0, NULL, &timingPool));
    
    ze_event_handle_t timingEvents[2];
    for (int i = 0; i < 2; i++) {
        ze_event_desc_t timingDesc = {
            .stype = ZE_STRUCTURE_TYPE_EVENT_DESC,
            .pNext = NULL,
            .index = i,
            .signal = ZE_EVENT_SCOPE_FLAG_HOST,
            .wait = ZE_EVENT_SCOPE_FLAG_HOST
        };
        CHECK_ZE(zeEventCreate(timingPool, &timingDesc, &timingEvents[i]));
        CHECK_ZE(zeEventHostReset(timingEvents[i]));
    }
    
    // Device timestamp operations (matching trace pattern)
    uint64_t hostTimestamp, deviceTimestamp;
    CHECK_ZE(zeDeviceGetGlobalTimestamps(device, &hostTimestamp, &deviceTimestamp));
    
    CHECK_ZE(zeCommandListAppendWriteGlobalTimestamp(cmdList, streamSharedMem, timingEvents[1], 0, NULL));
    CHECK_ZE(zeCommandListAppendMemoryCopy(cmdList, &deviceTimestamp, streamSharedMem, 8, timingEvents[0], 1, &timingEvents[1]));
    CHECK_ZE(zeCommandListAppendBarrier(cmdList, events[1], 1, &timingEvents[0]));
    
    // Second timing sequence
    ze_event_pool_desc_t timing2PoolDesc = {
        .stype = ZE_STRUCTURE_TYPE_EVENT_POOL_DESC,
        .pNext = NULL,
        .flags = ZE_EVENT_POOL_FLAG_HOST_VISIBLE,
        .count = 4
    };
    ze_event_pool_handle_t timing2Pool;
    CHECK_ZE(zeEventPoolCreate(context, &timing2PoolDesc, 0, NULL, &timing2Pool));
    
    ze_event_handle_t timing2Events[4];
    for (int i = 0; i < 4; i++) {
        ze_event_desc_t timing2Desc = {
            .stype = ZE_STRUCTURE_TYPE_EVENT_DESC,
            .pNext = NULL,
            .index = i,
            .signal = ZE_EVENT_SCOPE_FLAG_HOST,
            .wait = ZE_EVENT_SCOPE_FLAG_HOST
        };
        CHECK_ZE(zeEventCreate(timing2Pool, &timing2Desc, &timing2Events[i]));
        CHECK_ZE(zeEventHostReset(timing2Events[i]));
    }
    
    CHECK_ZE(zeCommandListAppendBarrier(cmdList, timing2Events[3], 1, &timingEvents[0]));
    
    CHECK_ZE(zeEventHostReset(events[2])); // Second hipEventRecord
    
    CHECK_ZE(zeDeviceGetGlobalTimestamps(device, &hostTimestamp, &deviceTimestamp));
    CHECK_ZE(zeCommandListAppendWriteGlobalTimestamp(cmdList, streamSharedMem, timing2Events[2], 1, &timing2Events[3]));
    CHECK_ZE(zeCommandListAppendMemoryCopy(cmdList, &deviceTimestamp, streamSharedMem, 8, timing2Events[1], 1, &timing2Events[2]));
    CHECK_ZE(zeCommandListAppendBarrier(cmdList, events[2], 1, &timing2Events[1]));
    CHECK_ZE(zeCommandListAppendBarrier(cmdList, timing2Events[0], 1, &timing2Events[1]));
    
    // === Phase 9: hipLaunchKernel → Kernel Creation and Launch (lines 198-279) ===
    printf("Creating and launching kernel to match trace...\n");
    
    // Create module from dummy binary (simplified - real trace has complex SPIR-V)
    ze_module_desc_t moduleDesc = {
        .stype = ZE_STRUCTURE_TYPE_MODULE_DESC,
        .pNext = NULL,
        .format = ZE_MODULE_FORMAT_NATIVE,
        .inputSize = sizeof(dummy_kernel_binary),
        .pInputModule = dummy_kernel_binary,
        .pBuildFlags = NULL,
        .pConstants = NULL
    };
    ze_module_handle_t module;
    ze_module_build_log_handle_t buildLog;
    
    // Note: This will likely fail with dummy binary, but matches trace pattern
    ze_result_t moduleResult = zeModuleCreate(context, device, &moduleDesc, &module, &buildLog);
    if (moduleResult != ZE_RESULT_SUCCESS) {
        printf("Module creation failed (expected with dummy binary) - proceeding with simulation\n");
        // Continue to simulate the rest of the pattern
        goto simulate_kernel_launch;
    }
    
    // Get kernel names and create kernels (lines 210-228)
    uint32_t kernelCount = 0;
    CHECK_ZE(zeModuleGetKernelNames(module, &kernelCount, NULL));
    
    const char **kernelNames = malloc(kernelCount * sizeof(char*));
    CHECK_ZE(zeModuleGetKernelNames(module, &kernelCount, kernelNames));
    
    // Create kernels for each name
    ze_kernel_handle_t kernels[4];
    for (uint32_t i = 0; i < kernelCount && i < 4; i++) {
        ze_kernel_desc_t kernelDesc = {
            .stype = ZE_STRUCTURE_TYPE_KERNEL_DESC,
            .pNext = NULL,
            .flags = ZE_KERNEL_FLAG_FORCE_RESIDENCY,
            .pKernelName = kernelNames[i]
        };
        CHECK_ZE(zeKernelCreate(module, &kernelDesc, &kernels[i]));
        
        ze_kernel_properties_t kernelProps = {.stype = ZE_STRUCTURE_TYPE_KERNEL_PROPERTIES};
        CHECK_ZE(zeKernelGetProperties(kernels[i], &kernelProps));
    }
    free(kernelNames);

simulate_kernel_launch:
    // === Phase 10: Kernel Launch Events and Setup (lines 229-279) ===
    ze_event_pool_desc_t kernelPoolDesc = {
        .stype = ZE_STRUCTURE_TYPE_EVENT_POOL_DESC,
        .pNext = NULL,
        .flags = ZE_EVENT_POOL_FLAG_HOST_VISIBLE,
        .count = 8
    };
    ze_event_pool_handle_t kernelPool;
    CHECK_ZE(zeEventPoolCreate(context, &kernelPoolDesc, 0, NULL, &kernelPool));
    
    ze_event_handle_t kernelEvents[8];
    for (int i = 0; i < 8; i++) {
        ze_event_desc_t kernelEventDesc = {
            .stype = ZE_STRUCTURE_TYPE_EVENT_DESC,
            .pNext = NULL,
            .index = i,
            .signal = ZE_EVENT_SCOPE_FLAG_HOST,
            .wait = ZE_EVENT_SCOPE_FLAG_HOST
        };
        CHECK_ZE(zeEventCreate(kernelPool, &kernelEventDesc, &kernelEvents[i]));
    }
    
    // Simulate kernel launch sequence (lines 249-279)
    if (moduleResult == ZE_RESULT_SUCCESS) {
        // Set kernel group size
        CHECK_ZE(zeKernelSetGroupSize(kernels[0], 1, 1, 1));
        CHECK_ZE(zeKernelSetIndirectAccess(kernels[0], ZE_KERNEL_INDIRECT_ACCESS_FLAG_HOST | ZE_KERNEL_INDIRECT_ACCESS_FLAG_DEVICE));
        
        CHECK_ZE(zeEventHostReset(kernelEvents[7]));
        
        ze_group_count_t launchArgs = {1, 1, 1};
        CHECK_ZE(zeCommandListAppendLaunchKernel(cmdList, kernels[0], &launchArgs, kernelEvents[7], 1, &timing2Events[0]));
        CHECK_ZE(zeEventHostSynchronize(kernelEvents[7], UINT64_MAX));
        CHECK_ZE(zeCommandQueueSynchronize(queue, UINT64_MAX));
        
        // Main kernel with parameters (addCountReverse kernel from trace)
        if (kernelCount > 1) {
            CHECK_ZE(zeKernelSetArgumentValue(kernels[1], 0, sizeof(void*), &deviceMem));
            CHECK_ZE(zeKernelSetArgumentValue(kernels[1], 1, sizeof(void*), &hostMem));
            size_t size = 10000000;
            CHECK_ZE(zeKernelSetArgumentValue(kernels[1], 2, sizeof(size_t), &size));
            int offset = 100;
            CHECK_ZE(zeKernelSetArgumentValue(kernels[1], 3, sizeof(int), &offset));
            
            CHECK_ZE(zeEventHostReset(kernelEvents[6]));
            CHECK_ZE(zeKernelSetGroupSize(kernels[1], 256, 1, 1));
            CHECK_ZE(zeKernelSetIndirectAccess(kernels[1], ZE_KERNEL_INDIRECT_ACCESS_FLAG_HOST | ZE_KERNEL_INDIRECT_ACCESS_FLAG_DEVICE));
            
            ze_group_count_t mainLaunchArgs = {39063, 1, 1}; // From trace
            CHECK_ZE(zeCommandListAppendLaunchKernel(cmdList, kernels[1], &mainLaunchArgs, kernelEvents[6], 0, NULL));
        }
    }
    
    // === Phase 11: Post-kernel Event Recording (lines 282-309) ===
    CHECK_ZE(zeEventHostReset(events[3])); // Final hipEventRecord
    
    // More timing operations
    CHECK_ZE(zeEventHostReset(kernelEvents[5]));
    CHECK_ZE(zeEventHostReset(kernelEvents[4]));
    CHECK_ZE(zeEventHostReset(kernelEvents[3]));
    
    CHECK_ZE(zeDeviceGetGlobalTimestamps(device, &hostTimestamp, &deviceTimestamp));
    CHECK_ZE(zeCommandListAppendWriteGlobalTimestamp(cmdList, streamSharedMem, kernelEvents[5], 1, &kernelEvents[6]));
    CHECK_ZE(zeCommandListAppendMemoryCopy(cmdList, &deviceTimestamp, streamSharedMem, 8, kernelEvents[4], 1, &kernelEvents[5]));
    CHECK_ZE(zeCommandListAppendBarrier(cmdList, events[3], 1, &kernelEvents[4]));
    CHECK_ZE(zeCommandListAppendBarrier(cmdList, kernelEvents[3], 1, &kernelEvents[4]));
    
    // === Phase 12: THE CRITICAL MOMENT - hipEventElapsedTime (line 310) ===
    printf("\n=== CRITICAL MOMENT: Simulating hipEventElapsedTime ===\n");
    printf("This should trigger the slow zeEventQueryStatus that we see in the trace...\n");
    
    // hipEventElapsedTime calls zeEventQueryStatus on both start and stop events
    // The trace shows this query taking 2.18ms!
    
    long start_time = get_time_microseconds();
    
    printf("Querying event status (this should potentially block for ~2ms like in trace)...\n");
    ze_result_t status = zeEventQueryStatus(events[2]); // Query the timing event
    
    long end_time = get_time_microseconds();
    long duration = end_time - start_time;
    
    printf("zeEventQueryStatus took: %ld microseconds\n", duration);
    printf("Trace showed: 2176 microseconds\n");
    printf("Event status: %s\n", 
           status == ZE_RESULT_NOT_READY ? "NOT_READY" : 
           status == ZE_RESULT_SUCCESS ? "SUCCESS" : "ERROR");
    
    if (duration > 1000) {
        printf("SUCCESS! Reproduced the blocking zeEventQueryStatus behavior!\n");
        printf("Duration %ld μs is similar to the 2176 μs seen in chipStar trace\n", duration);
    } else if (duration > 100) {
        printf("Partial success: Got elevated latency (%ld μs) but not as severe as trace\n", duration);
    } else {
        printf("Did not reproduce the blocking behavior (duration too short: %ld μs)\n", duration);
    }
    
    // === Cleanup ===
    printf("\nCleaning up...\n");
    
    // Cleanup events
    for (int i = 0; i < 8; i++) {
        zeEventDestroy(kernelEvents[i]);
    }
    zeEventPoolDestroy(kernelPool);
    
    for (int i = 0; i < 4; i++) {
        zeEventDestroy(timing2Events[i]);
        zeEventDestroy(events[i]);
        zeEventPoolDestroy(eventPools[i]);
    }
    zeEventPoolDestroy(timing2Pool);
    
    for (int i = 0; i < 2; i++) {
        zeEventDestroy(timingEvents[i]);
    }
    zeEventPoolDestroy(timingPool);
    
    zeEventDestroy(memsetEvent);
    zeEventPoolDestroy(memsetEventPool);
    
    // Cleanup kernels and module
    if (moduleResult == ZE_RESULT_SUCCESS) {
        for (uint32_t i = 0; i < 4; i++) {
            zeKernelDestroy(kernels[i]);
        }
        zeModuleDestroy(module);
    }
    
    // Cleanup memory
    zeMemFree(context, deviceMem);
    zeMemFree(context, hostMem);
    zeMemFree(context, sharedGlobal);
    zeMemFree(context, streamSharedMem);
    
    // Cleanup command objects
    zeCommandListDestroy(cmdList);
    zeCommandListDestroy(streamCmdList);
    zeCommandQueueDestroy(queue);
    zeCommandQueueDestroy(streamQueue);
    zeContextDestroy(context);
    
    printf("Test completed - check if Level Zero trace matches the original!\n");
    return 0;
} 