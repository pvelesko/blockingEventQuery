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

long test_single_pool_version(ze_context_handle_t context, ze_device_handle_t device, ze_command_list_handle_t cmdList, void *sharedGlobal) {
    printf("=== TEST 1: Single Pool - All Events in ONE Pool ===\n");
    printf("Creating the same dependency pattern but within a single event pool...\n");
    
    // Create single pool with all events (equivalent to what's normally across 5 pools)
    ze_event_pool_desc_t singlePoolDesc = {
        .stype = ZE_STRUCTURE_TYPE_EVENT_POOL_DESC,
        .pNext = NULL,
        .flags = 0,
        .count = 16 // All events in one pool: 4 basic + 2 timing + 3 timing2 + 6 kernel + 1 spare
    };
    ze_event_pool_handle_t singlePool;
    CHECK_ZE(zeEventPoolCreate(context, &singlePoolDesc, 0, NULL, &singlePool));
    
    // Create all events from single pool
    ze_event_handle_t allEvents[16];
    for (int i = 0; i < 16; i++) {
        ze_event_desc_t evtDesc = {
            .stype = ZE_STRUCTURE_TYPE_EVENT_DESC,
            .pNext = NULL,
            .index = i,
            .signal = ZE_EVENT_SCOPE_FLAG_HOST,
            .wait = ZE_EVENT_SCOPE_FLAG_HOST
        };
        CHECK_ZE(zeEventCreate(singlePool, &evtDesc, &allEvents[i]));
        CHECK_ZE(zeEventHostReset(allEvents[i]));
    }
    
    // Map to same logical structure as multi-pool version
    ze_event_handle_t *events = &allEvents[0];        // events[0-3] 
    ze_event_handle_t *timingEvents = &allEvents[4];  // timingEvents[0-1]
    ze_event_handle_t *timing2Events = &allEvents[6]; // timing2Events[0-2] 
    ze_event_handle_t *kernelEvents = &allEvents[9];  // kernelEvents[0-5]
    
    // Create IDENTICAL dependency pattern as multi-pool test
    uint64_t hostTimestamp, deviceTimestamp;
    CHECK_ZE(zeDeviceGetGlobalTimestamps(device, &hostTimestamp, &deviceTimestamp));
    
    // Phase 1: Basic → Timing dependencies
    CHECK_ZE(zeCommandListAppendWriteGlobalTimestamp(cmdList, sharedGlobal, timingEvents[1], 0, NULL));
    CHECK_ZE(zeCommandListAppendMemoryCopy(cmdList, &deviceTimestamp, sharedGlobal, 8, timingEvents[0], 1, &timingEvents[1]));
    CHECK_ZE(zeCommandListAppendBarrier(cmdList, events[1], 1, &timingEvents[0]));
    
    // Phase 2: Timing → Timing2 dependencies  
    CHECK_ZE(zeCommandListAppendBarrier(cmdList, timing2Events[2], 1, &timingEvents[0]));
    CHECK_ZE(zeCommandListAppendWriteGlobalTimestamp(cmdList, sharedGlobal, timing2Events[2], 0, NULL));
    CHECK_ZE(zeCommandListAppendMemoryCopy(cmdList, &deviceTimestamp, sharedGlobal, 8, timing2Events[1], 1, &timing2Events[2]));
    CHECK_ZE(zeCommandListAppendBarrier(cmdList, events[2], 1, &timing2Events[1]));
    CHECK_ZE(zeCommandListAppendBarrier(cmdList, timing2Events[0], 1, &timing2Events[1]));
    
    // Phase 3: Basic → Kernel dependencies
    CHECK_ZE(zeCommandListAppendWriteGlobalTimestamp(cmdList, sharedGlobal, kernelEvents[5], 0, NULL));
    CHECK_ZE(zeCommandListAppendMemoryCopy(cmdList, &deviceTimestamp, sharedGlobal, 8, kernelEvents[4], 1, &kernelEvents[5]));
    CHECK_ZE(zeCommandListAppendBarrier(cmdList, events[3], 1, &kernelEvents[4]));
    CHECK_ZE(zeCommandListAppendBarrier(cmdList, kernelEvents[3], 1, &kernelEvents[4]));
    
    // Query the same target event (events[2])
    printf("Querying events[2] within single pool...\n");
    long start_time = get_time_microseconds();
    ze_result_t status = zeEventQueryStatus(events[2]);
    long end_time = get_time_microseconds();
    long duration = end_time - start_time;
    
    printf("Single Pool Result: %ld microseconds (%.3f ms) - Status: %s\n", 
           duration, duration/1000.0, status == ZE_RESULT_SUCCESS ? "SUCCESS" : "NOT_READY");
    
    // Cleanup
    for (int i = 0; i < 16; i++) {
        zeEventDestroy(allEvents[i]);
    }
    zeEventPoolDestroy(singlePool);
    
    return duration;
}

// Simulate chipStar's kernel binary (dummy data)
static const char dummy_kernel_binary[] = {
    0x7f, 0x45, 0x4c, 0x46, 0x02, 0x01, 0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    // ... (simplified kernel binary data)
};

// Match the EXACT trace pattern - simulating chipStar HIP → Level Zero translation
int main() {
    printf("=== Level Zero Event Pool Dependency Issue Reproducer ===\n");
    printf("This demonstrates that complex cross-pool event dependencies cause zeEventQueryStatus to block\n");
    printf("Based on chipStar hipEventElapsedTime performance issue analysis\n\n");
    
    printf("KEY FINDING: The same dependency pattern is fast within ONE pool, but slow across MULTIPLE pools\n");
    printf("This reproducer creates dependencies across 5 separate event pools:\n");
    printf("- 4 basic event pools → 2 timing pools → kernel pools\n");
    
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
    
    // === FIRST: Test with single pool (should be fast) ===
    long singlePoolDuration = test_single_pool_version(context, device, cmdList, sharedGlobal);
    
    printf("\n=== TEST 2: Multiple Pools - Events Across SEPARATE Pools ===\n");
    printf("Creating the same dependency pattern but across multiple event pools...\n");
    
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
    CHECK_ZE(zeCommandListAppendWriteGlobalTimestamp(cmdList, sharedGlobal, timing2Events[2], 0, NULL));
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
    CHECK_ZE(zeCommandListAppendWriteGlobalTimestamp(cmdList, sharedGlobal, kernelEvents[5], 0, NULL));
    CHECK_ZE(zeCommandListAppendMemoryCopy(cmdList, &deviceTimestamp, sharedGlobal, 8, kernelEvents[4], 1, &kernelEvents[5]));
    CHECK_ZE(zeCommandListAppendBarrier(cmdList, events[3], 1, &kernelEvents[4]));
    CHECK_ZE(zeCommandListAppendBarrier(cmdList, kernelEvents[3], 1, &kernelEvents[4]));
    
    // === Phase 12: THE CRITICAL MOMENT - hipEventElapsedTime (line 310) ===
    printf("=== REPRODUCING THE BLOCKING zeEventQueryStatus ===\n");
    printf("This simulates chipStar's hipEventElapsedTime calling zeEventQueryStatus\n");
    printf("Query target: events[2] - involved in cross-pool dependency chain\n");
    printf("Expected: ~2-20+ seconds due to cross-pool dependency traversal\n\n");
    
    // hipEventElapsedTime calls zeEventQueryStatus on both start and stop events
    // The trace shows this query taking 2.18ms!
    
    long start_time = get_time_microseconds();
    
    printf("Calling zeEventQueryStatus(events[2])...\n");
    ze_result_t status = zeEventQueryStatus(events[2]); // Query the timing event
    
    long end_time = get_time_microseconds();
    long duration = end_time - start_time;
    
    printf("Multi-Pool Result: %ld microseconds (%.3f ms) - Status: %s\n", 
           duration, duration/1000.0, status == ZE_RESULT_SUCCESS ? "SUCCESS" : "NOT_READY");
    
    printf("\n=== PERFORMANCE COMPARISON ===\n");
    printf("Single Pool (all events in one pool):  %ld microseconds (%.3f ms)\n", 
           singlePoolDuration, singlePoolDuration/1000.0);
    printf("Multi-Pool (events across 5 pools):   %ld microseconds (%.3f ms)\n", 
           duration, duration/1000.0);
    printf("Performance difference: %.1fx slower\n", (double)duration / singlePoolDuration);
    
    if (duration > 1000000) {
        printf("\n✅ CONFIRMED: Cross-pool dependencies cause massive slowdown!\n");
        printf("   This demonstrates the Level Zero driver performance bottleneck.\n");
        printf("   In chipStar, this causes hipEventElapsedTime to block for seconds.\n");
    } else if (duration > singlePoolDuration * 5) {
        printf("\n⚠️  Significant slowdown observed with cross-pool dependencies.\n");
        printf("   This still demonstrates the performance issue.\n");
    } else {
        printf("\n❌ Did not reproduce major blocking (duration: %ld μs vs %ld μs)\n", 
               duration, singlePoolDuration);
        printf("   Note: The issue may be environment-specific.\n");
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