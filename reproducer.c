#include <level_zero/ze_api.h>
#include <stdio.h>
#include <stdlib.h>

int main() {
    ze_result_t result;
    
    // Initialize Level Zero driver
    result = zeInit(ZE_INIT_FLAG_GPU_ONLY);
    if (result != ZE_RESULT_SUCCESS) {
        printf("Failed to initialize Level Zero: %d\n", result);
        return -1;
    }
    
    // Discover drivers
    uint32_t driverCount = 0;
    result = zeDriverGet(&driverCount, NULL);
    if (result != ZE_RESULT_SUCCESS || driverCount == 0) {
        printf("No Level Zero drivers found\n");
        return -1;
    }
    
    ze_driver_handle_t* drivers = malloc(driverCount * sizeof(ze_driver_handle_t));
    result = zeDriverGet(&driverCount, drivers);
    if (result != ZE_RESULT_SUCCESS) {
        printf("Failed to get drivers\n");
        free(drivers);
        return -1;
    }
    
    // Get devices from first driver
    uint32_t deviceCount = 0;
    result = zeDeviceGet(drivers[0], &deviceCount, NULL);
    if (result != ZE_RESULT_SUCCESS || deviceCount == 0) {
        printf("No devices found\n");
        free(drivers);
        return -1;
    }
    
    ze_device_handle_t* devices = malloc(deviceCount * sizeof(ze_device_handle_t));
    result = zeDeviceGet(drivers[0], &deviceCount, devices);
    if (result != ZE_RESULT_SUCCESS) {
        printf("Failed to get devices\n");
        free(drivers);
        free(devices);
        return -1;
    }
    
    // Create context
    ze_context_desc_t contextDesc = {
        .stype = ZE_STRUCTURE_TYPE_CONTEXT_DESC,
        .pNext = NULL,
        .flags = 0
    };
    
    ze_context_handle_t context;
    result = zeContextCreate(drivers[0], &contextDesc, &context);
    if (result != ZE_RESULT_SUCCESS) {
        printf("Failed to create context\n");
        free(drivers);
        free(devices);
        return -1;
    }
    
    // Create command queue
    ze_command_queue_desc_t cmdQueueDesc = {
        .stype = ZE_STRUCTURE_TYPE_COMMAND_QUEUE_DESC,
        .pNext = NULL,
        .ordinal = 0,
        .index = 0,
        .flags = 0,
        .mode = ZE_COMMAND_QUEUE_MODE_DEFAULT,
        .priority = ZE_COMMAND_QUEUE_PRIORITY_NORMAL
    };
    
    ze_command_queue_handle_t cmdQueue;
    result = zeCommandQueueCreate(context, devices[0], &cmdQueueDesc, &cmdQueue);
    if (result != ZE_RESULT_SUCCESS) {
        printf("Failed to create command queue\n");
        zeContextDestroy(context);
        free(drivers);
        free(devices);
        return -1;
    }
    
    printf("Level Zero minimal program completed successfully\n");
    printf("Driver count: %d\n", driverCount);
    printf("Device count: %d\n", deviceCount);
    
    // Cleanup
    zeCommandQueueDestroy(cmdQueue);
    zeContextDestroy(context);
    free(drivers);
    free(devices);
    
    return 0;
}

