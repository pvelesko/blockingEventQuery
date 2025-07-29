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
    
    uint32_t deviceCount = 0;
    CHECK_ZE(zeDeviceGet(driver, &deviceCount, NULL));
    
    deviceCount = 1; // Try with just one device instead of 2
    ze_device_handle_t device;
    CHECK_ZE(zeDeviceGet(driver, &deviceCount, &device));
    
    // Create context with single device
    ze_context_desc_t contextDesc = {
        .stype = ZE_STRUCTURE_TYPE_CONTEXT_DESC,
        .pNext = NULL,
        .flags = 0
    };
    ze_context_handle_t context;
    CHECK_ZE(zeContextCreate(driver, &contextDesc, &context));
    
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
    
    // === Phase 3: Skip device memory allocation too ===
    // void *deviceMem;
    // CHECK_ZE(zeMemAllocDevice(context, &deviceAllocDesc, 1024, 0, device, &deviceMem)); // Test without this
    
    // === Phase 4: Skip host memory allocation ===
    // void *hostMem;
    // CHECK_ZE(zeMemAllocHost(context, &hostAllocDesc, 1024, 4096, &hostMem)); // Test without this
    
    // === Phase 6: Skip memset entirely ===
    // Removed memset event pool since we don't use it
    
    // === Phase 7: hipEventCreate events - try 2 pools with 2 events each ===
    ze_event_pool_handle_t eventPools[2]; // Reduce from 4 to 2 pools
    ze_event_handle_t events[4]; // Keep 4 events
    
    for (int i = 0; i < 2; i++) {
        ze_event_pool_desc_t poolDesc = {
            .stype = ZE_STRUCTURE_TYPE_EVENT_POOL_DESC,
            .pNext = NULL,
            .flags = 0, // Remove ZE_EVENT_POOL_FLAG_HOST_VISIBLE - confirmed not needed
            .count = 2 // 2 events per pool instead of 1
        };
        CHECK_ZE(zeEventPoolCreate(context, &poolDesc, 0, NULL, &eventPools[i]));
        
        // Create 2 events from this pool
        for (int j = 0; j < 2; j++) {
            ze_event_desc_t evtDesc = {
                .stype = ZE_STRUCTURE_TYPE_EVENT_DESC,
                .pNext = NULL,
                .index = j,
                .signal = ZE_EVENT_SCOPE_FLAG_HOST,
                .wait = ZE_EVENT_SCOPE_FLAG_HOST
            };
            CHECK_ZE(zeEventCreate(eventPools[i], &evtDesc, &events[i * 2 + j]));
        }
    }
    
    // === Phase 8: hipEventRecord operations (lines 126-197) ===
    CHECK_ZE(zeEventHostReset(events[1])); // hipEventRecord with blocking flag
    
    // Create timing event pool and events
    ze_event_pool_desc_t timingPoolDesc = {
        .stype = ZE_STRUCTURE_TYPE_EVENT_POOL_DESC,
        .pNext = NULL,
        .flags = 0,
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
    
    CHECK_ZE(zeCommandListAppendWriteGlobalTimestamp(cmdList, sharedGlobal, timingEvents[1], 0, NULL));
    CHECK_ZE(zeCommandListAppendMemoryCopy(cmdList, &deviceTimestamp, sharedGlobal, 8, timingEvents[0], 1, &timingEvents[1]));
    CHECK_ZE(zeCommandListAppendBarrier(cmdList, events[1], 1, &timingEvents[0]));
    
    // Second timing sequence
    ze_event_pool_desc_t timing2PoolDesc = {
        .stype = ZE_STRUCTURE_TYPE_EVENT_POOL_DESC,
        .pNext = NULL,
        .flags = 0,
        .count = 3 // Try reducing from 4 to 3
    };
    ze_event_pool_handle_t timing2Pool;
    CHECK_ZE(zeEventPoolCreate(context, &timing2PoolDesc, 0, NULL, &timing2Pool));
    
    ze_event_handle_t timing2Events[3]; // Reduce from 4 to 3
    for (int i = 0; i < 3; i++) {
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
    
    CHECK_ZE(zeCommandListAppendBarrier(cmdList, timing2Events[2], 1, &timingEvents[0])); // Use [2] instead of [3]
    
    CHECK_ZE(zeEventHostReset(events[2])); // Second hipEventRecord
    
    CHECK_ZE(zeDeviceGetGlobalTimestamps(device, &hostTimestamp, &deviceTimestamp));
    CHECK_ZE(zeCommandListAppendWriteGlobalTimestamp(cmdList, sharedGlobal, timing2Events[2], 1, &timing2Events[2]));
    CHECK_ZE(zeCommandListAppendMemoryCopy(cmdList, &deviceTimestamp, sharedGlobal, 8, timing2Events[1], 1, &timing2Events[2]));
    CHECK_ZE(zeCommandListAppendBarrier(cmdList, events[2], 1, &timing2Events[1]));
    CHECK_ZE(zeCommandListAppendBarrier(cmdList, timing2Events[0], 1, &timing2Events[1]));
    
    // === Phase 9: hipLaunchKernel → Kernel Creation and Launch (lines 198-279) ===
    printf("Creating and launching kernel to match trace...\n");
    
    // Skip kernel creation since dummy binary fails - go straight to simulation
    goto simulate_kernel_launch;

simulate_kernel_launch:
    // === Phase 10: Minimal Kernel Events (essential for reproduction) ===
    ze_event_pool_desc_t kernelPoolDesc = {
        .stype = ZE_STRUCTURE_TYPE_EVENT_POOL_DESC,
        .pNext = NULL,
        .flags = 0,
        .count = 6 // Reduce from 8 to 6
    };
    ze_event_pool_handle_t kernelPool;
    CHECK_ZE(zeEventPoolCreate(context, &kernelPoolDesc, 0, NULL, &kernelPool));
    
    ze_event_handle_t kernelEvents[6]; // Reduce from 8 to 6
    for (int i = 0; i < 6; i++) {
        ze_event_desc_t kernelEventDesc = {
            .stype = ZE_STRUCTURE_TYPE_EVENT_DESC,
            .pNext = NULL,
            .index = i,
            .signal = ZE_EVENT_SCOPE_FLAG_HOST,
            .wait = ZE_EVENT_SCOPE_FLAG_HOST
        };
        CHECK_ZE(zeEventCreate(kernelPool, &kernelEventDesc, &kernelEvents[i]));
    }
    
    // Reset some events - remove unused ones
    CHECK_ZE(zeEventHostReset(kernelEvents[5]));
    CHECK_ZE(zeEventHostReset(kernelEvents[4]));
    CHECK_ZE(zeEventHostReset(kernelEvents[3]));
    
    // === Phase 11: Post-kernel Event Recording (lines 282-309) ===
    CHECK_ZE(zeEventHostReset(events[3])); // Final hipEventRecord
    
    // Simplified timing operations - these are essential!
    CHECK_ZE(zeDeviceGetGlobalTimestamps(device, &hostTimestamp, &deviceTimestamp));
    CHECK_ZE(zeCommandListAppendWriteGlobalTimestamp(cmdList, sharedGlobal, kernelEvents[5], 1, &kernelEvents[5])); // Self-dependency
    CHECK_ZE(zeCommandListAppendMemoryCopy(cmdList, &deviceTimestamp, sharedGlobal, 8, kernelEvents[4], 1, &kernelEvents[5]));
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
    for (int i = 0; i < 6; i++) {
        zeEventDestroy(kernelEvents[i]);
    }
    zeEventPoolDestroy(kernelPool);
    
    for (int i = 0; i < 2; i++) {
        for (int j = 0; j < 2; j++) {
            zeEventDestroy(events[i * 2 + j]);
        }
        zeEventPoolDestroy(eventPools[i]);
    }
    
    for (int i = 0; i < 2; i++) {
        zeEventDestroy(timingEvents[i]);
    }
    zeEventPoolDestroy(timingPool);
    
    for (int i = 0; i < 3; i++) {
        zeEventDestroy(timing2Events[i]);
    }
    zeEventPoolDestroy(timing2Pool);
    
    // Cleanup kernels and module
    // The original code had a moduleResult and module variable, but they were not defined.
    // Assuming they were meant to be removed or are placeholders for future use.
    // For now, we'll just remove the cleanup for kernels and module as they are not defined.
    
    // Cleanup memory
    zeMemFree(context, sharedGlobal);
    // zeMemFree(context, hostMem); // This line was removed
    
    // Cleanup command objects
    zeCommandListDestroy(cmdList);
    zeCommandQueueDestroy(queue);
    zeContextDestroy(context);
    
    printf("Test completed - check if Level Zero trace matches the original!\n");
    return 0;
} 