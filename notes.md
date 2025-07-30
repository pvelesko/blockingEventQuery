# Reproduce Level Zero Blocking Event Query

I have a chipStar/HIP sample:

```
╰─$ make run                                                                                                                             127 ↵
./test
test: starting sequence with sizeBytes=40000000 bytes, 38.147 MB
CHIP error [TID 2231289] [1753849873.932144867] : CHIPEventLevel0::getSyncQueuesLastEvents() events to wait on for target event memFill
test 0x1001: stream=0 waitStart=0 syncMode=syncNone
CHIP error [TID 2231289] [1753849873.949474951] : CHIPEventLevel0::getSyncQueuesLastEvents() events to wait on for target event recordEvent:timestampWrite
CHIP error [TID 2231289] [1753849873.953190938] : CHIPEventLevel0::getSyncQueuesLastEvents() events to wait on for target event launch __chip_reset_non_symbols
CHIP error [TID 2231289] [1753849873.953212123] :   - recordEvent:complete
CHIP error [TID 2231289] [1753849873.953925587] : CHIPEventLevel0::getSyncQueuesLastEvents() events to wait on for target event launch _Z15addCountReverseIiEvPKT_PS0_li
CHIP error [TID 2231289] [1753849873.953936093] :   - recordEvent:complete
CHIP error [TID 2231289] [1753849873.954035841] : CHIPEventLevel0::getSyncQueuesLastEvents() events to wait on for target event recordEvent:timestampWrite
CHIP error [TID 2231289] [1753849873.954043572] :   - launch _Z15addCountReverseIiEvPKT_PS0_li
CHIP critical [TID 2231289] [1753849874.132082302] : zeEventQueryStatus took 177866 microseconds (>100 microseconds threshold)
make: *** [Makefile:26: run] Aborted
```

As you can see zeEventQueryStatus is blocking!

The Goal: Make a pure Level Zero reproducer. 


1. The HIP sample and how to get the trace is attached
2. The SPIR-V kernel that the sample uses is attached. 
3. Create a level zero prgoram that has exacrtly the same number of level zero API calls as the sample
4. Use iprof to verify