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

// Reproduce the EXACT pattern from singlePoolBlocking.trace WITH MASSIVE COMPLEXITY
int main() {
    printf("=== MASSIVE Single Pool Blocking zeEventQueryStatus Reproducer ===\n");
    printf("Creating extreme complexity to trigger the blocking behavior\n");
    printf("- Single large event pool (1000 events)\n");
    printf("- Hundreds of events with complex cross-dependencies\n");
    printf("- Simulating real chipStar workload complexity\n");
    printf("- Expected: ~2ms blocking on zeEventQueryStatus\n\n");
    
    // Initialize Level Zero driver 
    CHECK_ZE(zeInit(0));
    
    uint32_t driverCount = 1;
    ze_driver_handle_t driver;
    CHECK_ZE(zeDriverGet(&driverCount, &driver));
    
    uint32_t deviceCount = 1;
    ze_device_handle_t device;
    CHECK_ZE(zeDeviceGet(driver, &deviceCount, &device));
    
    // Create context
    ze_context_desc_t contextDesc = {
        .stype = ZE_STRUCTURE_TYPE_CONTEXT_DESC,
        .pNext = NULL,
        .flags = 0
    };
    ze_context_handle_t context;
    CHECK_ZE(zeContextCreate(driver, &contextDesc, &context));
    
    // Allocate shared memory (like in trace)
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
    
    // Allocate device memory (40MB like in trace)
    void *deviceMem;
    CHECK_ZE(zeMemAllocDevice(context, &deviceAllocDesc, 40000000, 0, device, &deviceMem));
    
    // Allocate host memory (40MB like in trace)  
    void *hostMem;
    CHECK_ZE(zeMemAllocHost(context, &hostAllocDesc, 40000000, 4096, &hostMem));
    
    // Query command queue group properties to find copy engines
    uint32_t queueGroupCount = 0;
    CHECK_ZE(zeDeviceGetCommandQueueGroupProperties(device, &queueGroupCount, NULL));
    
    ze_command_queue_group_properties_t *queueGroupProps = 
        (ze_command_queue_group_properties_t*)malloc(queueGroupCount * sizeof(ze_command_queue_group_properties_t));
    for (uint32_t i = 0; i < queueGroupCount; i++) {
        queueGroupProps[i].stype = ZE_STRUCTURE_TYPE_COMMAND_QUEUE_GROUP_PROPERTIES;
        queueGroupProps[i].pNext = NULL;
    }
    CHECK_ZE(zeDeviceGetCommandQueueGroupProperties(device, &queueGroupCount, queueGroupProps));
    
    printf("Found %d command queue groups:\n", queueGroupCount);
    uint32_t computeOrdinal = 0, copyOrdinal = UINT32_MAX;
    
    for (uint32_t i = 0; i < queueGroupCount; i++) {
        printf("  Group %d: flags=0x%x, numQueues=%d", i, queueGroupProps[i].flags, queueGroupProps[i].numQueues);
        
        if (queueGroupProps[i].flags & ZE_COMMAND_QUEUE_GROUP_PROPERTY_FLAG_COMPUTE) {
            printf(" [COMPUTE]");
            computeOrdinal = i;
        }
        if (queueGroupProps[i].flags & ZE_COMMAND_QUEUE_GROUP_PROPERTY_FLAG_COPY) {
            printf(" [COPY]");
            if (copyOrdinal == UINT32_MAX) copyOrdinal = i;
        }
        printf("\n");
    }
    
    // Create compute queue (using existing logic)
    ze_command_queue_desc_t queueDesc = {
        .stype = ZE_STRUCTURE_TYPE_COMMAND_QUEUE_DESC,
        .pNext = NULL,
        .ordinal = computeOrdinal,
        .index = 0,
        .flags = 0,
        .mode = ZE_COMMAND_QUEUE_MODE_ASYNCHRONOUS,
        .priority = ZE_COMMAND_QUEUE_PRIORITY_NORMAL
    };
    ze_command_queue_handle_t queue;
    CHECK_ZE(zeCommandQueueCreate(context, device, &queueDesc, &queue));
    
    ze_command_list_handle_t cmdList;
    CHECK_ZE(zeCommandListCreateImmediate(context, device, &queueDesc, &cmdList));
    
    // Try to create a copy queue if available
    ze_command_queue_handle_t copyQueue = NULL;
    ze_command_list_handle_t copyCmdList = NULL;
    
    if (copyOrdinal != UINT32_MAX) {
        printf("Creating dedicated copy queue on ordinal %d...\n", copyOrdinal);
        
        ze_command_queue_desc_t copyQueueDesc = {
            .stype = ZE_STRUCTURE_TYPE_COMMAND_QUEUE_DESC,
            .pNext = NULL,
            .ordinal = copyOrdinal,
            .index = 0,
            .flags = 0,
            .mode = ZE_COMMAND_QUEUE_MODE_ASYNCHRONOUS,
            .priority = ZE_COMMAND_QUEUE_PRIORITY_NORMAL
        };
        
        ze_result_t result = zeCommandQueueCreate(context, device, &copyQueueDesc, &copyQueue);
        if (result == ZE_RESULT_SUCCESS) {
            printf("✅ Successfully created copy queue!\n");
            
            // Create immediate command list for copy operations
            result = zeCommandListCreateImmediate(context, device, &copyQueueDesc, &copyCmdList);
            if (result == ZE_RESULT_SUCCESS) {
                printf("✅ Successfully created copy command list!\n");
            } else {
                printf("❌ Failed to create copy command list: %d\n", result);
            }
        } else {
            printf("❌ Failed to create copy queue: %d\n", result);
        }
    } else {
        printf("❌ No dedicated COPY queue group found\n");
    }
    
    free(queueGroupProps);
    
    // === CREATE MASSIVE COMPLEXITY ===
    printf("Creating large event pool with 1000 events...\n");
    
    // Create the EXACT same large pool as in trace (line 85)
    ze_event_pool_desc_t poolDesc = {
        .stype = ZE_STRUCTURE_TYPE_EVENT_POOL_DESC,
        .pNext = NULL,
        .flags = ZE_EVENT_POOL_FLAG_HOST_VISIBLE,  // Match trace exactly
        .count = 1000  // EXACTLY like singlePoolBlocking.trace line 85
    };
    ze_event_pool_handle_t eventPool;
    CHECK_ZE(zeEventPoolCreate(context, &poolDesc, 0, NULL, &eventPool));
    
    // Create memset event (index 0, like trace line 87)
    ze_event_desc_t memsetEventDesc = {
        .stype = ZE_STRUCTURE_TYPE_EVENT_DESC,
        .pNext = NULL,
        .index = 0,
        .signal = ZE_EVENT_SCOPE_FLAG_HOST,
        .wait = ZE_EVENT_SCOPE_FLAG_HOST
    };
    ze_event_handle_t memsetEvent;
    CHECK_ZE(zeEventCreate(eventPool, &memsetEventDesc, &memsetEvent));
    CHECK_ZE(zeEventHostReset(memsetEvent));
    
    // Do the memset operation like in trace (lines 91-98)
    printf("Performing memset operation like in trace...\n");
    CHECK_ZE(zeCommandListAppendMemoryFill(cmdList, deviceMem, "\x00", 1, 40000000, memsetEvent, 0, NULL));
    CHECK_ZE(zeEventHostSynchronize(memsetEvent, UINT64_MAX));
    CHECK_ZE(zeCommandQueueSynchronize(queue, UINT64_MAX));
    
    // === CREATE HUNDREDS OF EVENTS WITH MASSIVE COMPLEXITY ===
    printf("Creating hundreds of events with complex dependencies...\n");
    
    #define MAX_EVENTS 500  // Create 500 events to simulate real workload
    ze_event_handle_t events[MAX_EVENTS];
    
    // Create many events
    for (int i = 1; i < MAX_EVENTS; i++) {
        ze_event_desc_t evtDesc = {
            .stype = ZE_STRUCTURE_TYPE_EVENT_DESC,
            .pNext = NULL,
            .index = i,
            .signal = ZE_EVENT_SCOPE_FLAG_HOST,
            .wait = ZE_EVENT_SCOPE_FLAG_HOST
        };
        CHECK_ZE(zeEventCreate(eventPool, &evtDesc, &events[i]));
        CHECK_ZE(zeEventHostReset(events[i]));
    }
    
    printf("Building MASSIVE dependency chain across %d events...\n", MAX_EVENTS-1);
    
    // Build extremely complex dependency patterns
    uint64_t hostTimestamp, deviceTimestamp;
    
    // Create complex dependency chains between events
    for (int phase = 0; phase < 10; phase++) {
        printf("Creating dependency phase %d...\n", phase + 1);
        
        for (int i = 1 + phase * 40; i < (MAX_EVENTS - 20 < 1 + (phase + 1) * 40 ? MAX_EVENTS - 20 : 1 + (phase + 1) * 40); i++) {
            if (i + 10 < MAX_EVENTS) {
                CHECK_ZE(zeDeviceGetGlobalTimestamps(device, &hostTimestamp, &deviceTimestamp));
                
                // Create complex dependency patterns
                CHECK_ZE(zeCommandListAppendWriteGlobalTimestamp(cmdList, sharedGlobal, events[i + 5], 0, NULL));
                CHECK_ZE(zeCommandListAppendMemoryCopy(cmdList, &deviceTimestamp, sharedGlobal, 8, events[i + 6], 1, &events[i + 5]));
                CHECK_ZE(zeCommandListAppendBarrier(cmdList, events[i], 1, &events[i + 6]));
                CHECK_ZE(zeCommandListAppendBarrier(cmdList, events[i + 7], 1, &events[i + 6]));
                
                // Add more barriers to create complex dependencies
                CHECK_ZE(zeCommandListAppendBarrier(cmdList, events[i + 8], 1, &events[i + 7]));
                CHECK_ZE(zeCommandListAppendBarrier(cmdList, events[i + 9], 1, &events[i + 8]));
                
                // Create cross-dependencies
                if (i > 10) {
                    CHECK_ZE(zeCommandListAppendBarrier(cmdList, events[i], 1, &events[i - 10]));
                }
                if (i > 20) {
                    CHECK_ZE(zeCommandListAppendBarrier(cmdList, events[i], 1, &events[i - 20]));
                }
            }
        }
    }
    
    // Create even MORE complex cross-dependencies 
    printf("Adding cross-dependencies between distant events...\n");
    for (int i = 50; i < MAX_EVENTS - 50; i += 10) {
        for (int j = 1; j < 5; j++) {
            if (i + j * 10 < MAX_EVENTS) {
                CHECK_ZE(zeCommandListAppendBarrier(cmdList, events[i + j * 10], 1, &events[i]));
            }
        }
    }
    
    // Add timing operations throughout 
    printf("Adding timing operations...\n");
    for (int i = 100; i < MAX_EVENTS - 100; i += 50) {
        CHECK_ZE(zeDeviceGetGlobalTimestamps(device, &hostTimestamp, &deviceTimestamp));
        CHECK_ZE(zeCommandListAppendWriteGlobalTimestamp(cmdList, sharedGlobal, events[i + 10], 0, NULL));
        CHECK_ZE(zeCommandListAppendMemoryCopy(cmdList, &deviceTimestamp, sharedGlobal, 8, events[i + 20], 1, &events[i + 10]));
        
        // Create dependency chains
        for (int k = 0; k < 5; k++) {
            if (i + 20 + k < MAX_EVENTS) {
                CHECK_ZE(zeCommandListAppendBarrier(cmdList, events[i + 20 + k], 1, &events[i + 19 + k]));
            }
        }
    }
    
    // Create a final complex web of dependencies centered around our target event
    int targetEvent = 250;  // Middle of our range
    printf("Creating final complex web around target event %d...\n", targetEvent);
    
    for (int i = targetEvent - 50; i < targetEvent + 50; i++) {
        if (i > 0 && i < MAX_EVENTS - 1 && i != targetEvent) {
            // Create dependencies to/from target event
            CHECK_ZE(zeCommandListAppendBarrier(cmdList, events[targetEvent], 1, &events[i]));
            
            // Create dependencies between nearby events  
            if (i + 1 < MAX_EVENTS) {
                CHECK_ZE(zeCommandListAppendBarrier(cmdList, events[i + 1], 1, &events[i]));
            }
        }
    }
    
    printf("=== REPRODUCING THE BLOCKING zeEventQueryStatus ===\n");
    printf("Query target: events[%d] - center of massive dependency web\n", targetEvent);
    printf("Total events: %d\n", MAX_EVENTS - 1);
    printf("Expected: Significant blocking due to extreme dependency complexity\n\n");
    
    // THE CRITICAL MOMENT - Query events multiple times to see timing patterns
    int testEvents[3] = {targetEvent, targetEvent - 1, targetEvent + 1};
    
    for (int eventIdx = 0; eventIdx < 3; eventIdx++) {
        int currentEvent = testEvents[eventIdx];
        printf("\n=== Testing Event[%d] (3 queries) ===\n", currentEvent);
        
        for (int queryNum = 1; queryNum <= 3; queryNum++) {
            printf("Query #%d of events[%d]: ", queryNum, currentEvent);
            long start_time = get_time_microseconds();
            ze_result_t status = zeEventQueryStatus(events[currentEvent]);
            long end_time = get_time_microseconds();
            long duration = end_time - start_time;
            
            printf("%ld μs (%.3f ms) - Status: %s\n", 
                   duration, duration/1000.0, 
                   status == ZE_RESULT_SUCCESS ? "SUCCESS" : "NOT_READY");
                   
            // Brief analysis after first query of each event
            if (queryNum == 1) {
                if (duration > 1000) {  // More than 1ms
                    printf("  ✅ Reproduced blocking behavior! Duration: %.3f ms\n", duration/1000.0);
                } else if (duration > 100) {  // More than 100μs
                    printf("  ⚠️  Significant slowdown: %.3f ms\n", duration/1000.0);
                } else {
                    printf("  ℹ️  Fast query: %ld μs\n", duration);
                }
            }
        }
    }
    
    // Cleanup
    printf("\nCleaning up...\n");
    
    for (int i = 1; i < MAX_EVENTS; i++) {
        zeEventDestroy(events[i]);
    }
    zeEventDestroy(memsetEvent);
    zeEventPoolDestroy(eventPool);
    
    zeMemFree(context, sharedGlobal);
    zeMemFree(context, deviceMem);
    zeMemFree(context, hostMem);
    
    if (copyCmdList) zeCommandListDestroy(copyCmdList);
    if (copyQueue) zeCommandQueueDestroy(copyQueue);
    
    zeCommandListDestroy(cmdList);
    zeCommandQueueDestroy(queue);
    zeContextDestroy(context);
    
    printf("Test completed - massive complexity dependency reproduction!\n");
    return 0;
} 