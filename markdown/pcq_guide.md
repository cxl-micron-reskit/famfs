# PCQ - Producer/Consumer Queue Utility Guide

## Table of Contents

1. [Introduction to PCQ](#1-introduction-to-pcq)
2. [PCQ Architecture and Internal Details](#2-pcq-architecture-and-internal-details)
3. [Purpose of PCQ](#3-purpose-of-pcq)
4. [Running PCQ on a Single Host with a DAX Device](#4-running-pcq-on-a-single-host-with-a-dax-device)
5. [Running PCQ on Two Hosts Sharing a DAX Device](#5-running-pcq-on-two-hosts-sharing-a-dax-device)
6. [Challenges with Cache Coherency and PCQ](#6-challenges-with-cache-coherency-and-pcq)

---

## 1. Introduction to PCQ

PCQ (Producer/Consumer Queue) is a utility within the famfs (Fabric-Attached Memory File System) project that implements a high-performance message queue over shared memory. It is designed to test and demonstrate inter-process and inter-node communication using memory-mapped files backed by DAX (Direct Access) devices.

The PCQ utility allows:
- Creating shared memory queues with configurable bucket sizes and counts
- Running producer processes that write messages to the queue
- Running consumer processes that read messages from the queue
- Testing shared memory communication between processes on the same host or across different hosts sharing the same memory

PCQ serves as both a testing tool for famfs shared memory capabilities and a reference implementation for producer/consumer patterns over fabric-attached memory.

### Key Features

- **Circular buffer implementation**: Messages are stored in a fixed-size circular buffer with configurable bucket sizes
- **CRC validation**: Each message includes a CRC32 checksum to verify data integrity
- **Sequence numbering**: Messages are sequentially numbered to detect ordering issues
- **Cache coherency handling**: Explicit CPU cache flush and invalidate operations for cross-node communication
- **Permission control**: Fine-grained access control for producer and consumer roles
- **Flexible operation modes**: Support for message count-based runs, timed runs, and drain operations

---

## 2. PCQ Architecture and Internal Details

### 2.1 File Structure

A PCQ queue consists of two files:

1. **Producer file** (`<queuename>`): Contains the queue metadata and the bucket array
2. **Consumer file** (`<queuename>.consumer`): Contains the consumer's state information

This separation allows producers and consumers to have independent write access to their respective state while maintaining read access to each other's state.

### 2.2 Memory Layout

The queue uses a 2MB offset for the bucket array to ensure PMD (Page Middle Directory) alignment, which is critical for efficient huge page fault handling in famfs. The total producer file size is:

```
size = 2MB + (nbuckets * bucket_size)
```

The consumer file is fixed at 2MB.

### 2.3 Bucket Format

Each bucket in the queue has the following format:

```
+----------------------------------+
| Payload (bucket_size - 16 bytes) |
+----------------------------------+
| Sequence Number (8 bytes)        |
+----------------------------------+
| CRC32 Checksum (8 bytes)         |
+----------------------------------+
```

The payload size is therefore: `bucket_size - sizeof(u64) - sizeof(unsigned long)`

### 2.4 Queue Operations

#### Queue Creation (`pcq_create`)

1. Verify bucket_size is a power of 2
2. Check that neither producer nor consumer file exists
3. Create consumer file via `famfs_mkfile()` with 2MB size
4. Memory-map the consumer file and initialize:
   - Set magic number
   - Initialize consumer_index to 0
   - Initialize next_seq to 0
   - Flush processor cache
5. Create producer file with calculated size
6. Memory-map the producer file and initialize:
   - Set magic number
   - Set nbuckets and bucket_size
   - Set bucket_array_offset to 2MB
   - Initialize producer_index to 0
   - Initialize next_seq to 0
   - Flush processor cache

#### Producer Put Operation (`pcq_producer_put`)

1. **Check for full queue**: Compare `(producer_index + 1) % nbuckets` with `consumer_index`
   - If full and `wait` mode: invalidate cache for consumer_index and yield
   - If full and no-wait mode: return `PCQ_PUT_FULL_NOWAIT`
2. **Prepare the entry**:
   - Assign sequence number from `pcq->next_seq++`
   - Calculate CRC32 over payload + sequence number
   - Store CRC at the end of the entry
3. **Write to queue**:
   - Calculate bucket address: `pcq + bucket_array_offset + (put_index * bucket_size)`
   - Copy entry to bucket
   - Flush processor cache for the bucket
4. **Update producer index**:
   - Set `producer_index = (put_index + 1) % nbuckets`
   - Flush processor cache for producer_index

#### Consumer Get Operation (`pcq_consumer_get`)

1. **Check for empty queue**: Compare `consumer_index` with `producer_index`
   - Invalidate cache for producer_index before reading
   - If empty and `wait` mode: yield and retry
   - If empty and no-wait mode: return `PCQ_GET_EMPTY`
2. **Read from queue**:
   - Calculate bucket address
   - Copy bucket contents to output buffer
3. **Validate the entry**:
   - Calculate CRC32 and compare with stored CRC
   - If CRC mismatch: invalidate cache for bucket, retry up to 2 times
   - Verify sequence number matches expected value
4. **Update consumer state**:
   - Increment consumer_index: `(consumer_index + 1) % nbuckets`
   - Flush processor cache for consumer_index
   - Increment next_seq

### 2.6 Threading Model

The PCQ utility supports running producer and consumer as separate threads within the same process or as separate processes:

- **Single process mode** (`-pc`): Creates separate producer and consumer threads that communicate through the queue
- **Separate process mode**: Run `pcq --producer` on one process/node and `pcq --consumer` on another

An optional **status thread** can periodically report queue statistics when the `-s <interval>` option is used.

### 2.7 Stop Modes

PCQ supports three stop modes:

1. **NMESSAGES**: Stop after sending/receiving N messages
2. **STOP_FLAG**: Stop when the runtime expires (timed mode)
3. **EMPTY**: Consumer only - stop when the queue is empty (drain mode)

---

## 3. Purpose of PCQ

### 3.1 Testing Shared Memory Infrastructure

PCQ's primary purpose is to validate that famfs correctly implements shared memory semantics across multiple hosts. It tests:

- **Memory mapping**: Verifying that `famfs_mmap_whole_file()` correctly maps shared memory
- **File creation**: Testing `famfs_mkfile()` for creating memory-backed files
- **Cache coherency**: Ensuring that cache flush/invalidate operations work correctly
- **Data integrity**: Validating that data written by one host can be read correctly by another

### 3.2 Demonstrating Inter-Node Communication

PCQ demonstrates a practical use case for fabric-attached memory: message passing between nodes without traditional network overhead. This is relevant for:

- High-performance computing (HPC) applications
- Distributed databases
- Real-time data processing systems
- Any application requiring low-latency inter-node communication

### 3.3 Benchmarking

PCQ can be used to benchmark:

- Message throughput (messages per second)
- Latency characteristics
- Cache coherency overhead (via retry counts)
- Impact of bucket sizes on performance

### 3.4 Reference Implementation

PCQ serves as a reference implementation for developers building applications on famfs, demonstrating:

- Proper use of cache coherency primitives
- Circular buffer implementation in shared memory
- Error handling for shared memory scenarios
- Thread-safe producer/consumer patterns

---

## 4. Running PCQ on a Single Host with a DAX Device

### 4.1 Prerequisites

1. A famfs-enabled Linux kernel
2. Famfs user space tools installed
3. A DAX device (e.g., `/dev/dax0.0`) of at least 4GB
4. A mounted famfs file system

### 4.2 Setup Steps

#### Step 1: Load the famfs kernel module

```bash
sudo modprobe famfs
```

#### Step 2: Create and mount a famfs file system

```bash
# Format the dax device
sudo mkfs.famfs /dev/dax0.0

# Create mount point
sudo mkdir -p /mnt/famfs

# Mount the file system
sudo famfs mount /dev/dax0.0 /mnt/famfs

# Play the log to instantiate files
sudo famfs logplay /mnt/famfs
```

#### Step 3: Verify the mount

```bash
mount | grep famfs
```

### 4.3 Creating a Queue

Create a queue with 1024 buckets of 1KB each:

```bash
sudo pcq --create --bsize 1024 --nbuckets 1024 /mnt/famfs/myqueue
```


Verify the files were created:
```bash
ls -la /mnt/famfs/myqueue*
```

### 4.4 Running Producer and Consumer Together

Run producer and consumer in the same process for 1000 messages:

```bash
# First set appropriate permissions
sudo chown $(id -un):$(id -gn) /mnt/famfs/myqueue /mnt/famfs/myqueue.consumer

# Run producer and consumer together
pcq --producer --consumer --nmessages 1000 /mnt/famfs/myqueue
```


### 4.5 Running Producer and Consumer Separately

In terminal 1 (start consumer first, it will wait for messages):
```bash
pcq --consumer --nmessages 1000 /mnt/famfs/myqueue
```

In terminal 2 (start producer):
```bash
pcq --producer --nmessages 1000 /mnt/famfs/myqueue
```

### 4.6 Timed Run with Status Updates

Run for 10 seconds with status updates every second:

```bash
pcq -pc --time 10 --status 1 /mnt/famfs/myqueue
```

**Expected output:**
```
03-25 10:15:01 pcq=/mnt/famfs/myqueue prod(nsent=125432 nfull=0) cons(nrcvd=125430 nempty=2 nretries=0 nerrors=0)
03-25 10:15:02 pcq=/mnt/famfs/myqueue prod(nsent=251023 nfull=0) cons(nrcvd=251020 nempty=3 nretries=0 nerrors=0)
...
pcq:    /mnt/famfs/myqueue
pcq producer: nsent=1254320 nerrors=0 nfull=0
pcq consumer: nreceived=1254320 nerrors=0 nempty=15 retries=0
```

### 4.7 Checking Queue State

```bash
pcq --info /mnt/famfs/myqueue
```

**Expected output:**
```
get_queue_info: queue /mnt/famfs/myqueue contains 0 messages p next_seq 1254320 c next_seq 1254320
```

### 4.8 Draining a Queue

If the queue has remaining messages:

```bash
pcq --drain /mnt/famfs/myqueue
```

**Expected output:**
```
pcq:    /mnt/famfs/myqueue
pcq drain: nreceived=0 nerrors=0 nempty=1 retries=0
```

### 4.9 Data Verification with Seed

Use a seed to generate random payload and verify on consumption:

```bash
pcq -pc --seed 42 --nmessages 10000 /mnt/famfs/myqueue
```

If data corruption occurs, you would see:
```
run_consumer: miscompare seq=1234 ofs=128
```

---

## 5. Running PCQ on Two Hosts Sharing a DAX Device

### 5.1 Prerequisites

1. Two hosts (Host A and Host B) with shared access to the same memory device
2. Both hosts running famfs-enabled kernels
3. The shared memory device visible on both hosts (e.g., via CXL fabric-attached memory)
4. Famfs user space tools installed on both hosts

### 5.2 Architecture Overview

```
+------------------+          +------------------+
|     Host A       |          |     Host B       |
|   (Producer)     |          |   (Consumer)     |
+------------------+          +------------------+
        |                             |
        |     Shared DAX Memory       |
        |   +---------------------+   |
        +-->|  /dev/dax0.0        |<--+
            |  (Fabric-Attached)  |
            +---------------------+
```

### 5.3 Setup Steps

#### On Host A (Master/Producer):

**Step 1: Format and mount famfs**

```bash
sudo modprobe famfs

# Format the shared dax device
sudo mkfs.famfs /dev/dax0.0

# Mount the file system
sudo mkdir -p /mnt/famfs
sudo famfs mount /dev/dax0.0 /mnt/famfs
sudo famfs logplay /mnt/famfs
```

**Step 2: Create the queue**

```bash
sudo pcq --create --bsize 4096 --nbuckets 4096 /mnt/famfs/shared_queue
```

**Step 3: Set permissions for producer role**

```bash
sudo pcq --setperm p /mnt/famfs/shared_queue
```

This sets:
- Producer file (`shared_queue`): read-write (0644)
- Consumer file (`shared_queue.consumer`): read-only (0444)

#### On Host B (Client/Consumer):

**Step 1: Mount famfs (read-only metadata access)**

```bash
sudo modprobe famfs

# Mount the file system (Host B sees it as a client)
sudo mkdir -p /mnt/famfs
sudo famfs mount /dev/dax0.0 /mnt/famfs
sudo famfs logplay /mnt/famfs
```

**Step 2: Verify the queue files are visible**

```bash
ls -la /mnt/famfs/shared_queue*
```

**Step 3: Set permissions for consumer role**

```bash
sudo pcq --setperm c /mnt/famfs/shared_queue
```

This sets:
- Producer file (`shared_queue`): read-only (0444)
- Consumer file (`shared_queue.consumer`): read-write (0644)

### 5.4 Running the Test

**On Host B (Consumer) - Start first:**

```bash
pcq --consumer --nmessages 10000 --seed 42 -v /mnt/famfs/shared_queue
```

The consumer will wait for messages to arrive.

**On Host A (Producer) - Start after consumer is ready:**

```bash
pcq --producer --nmessages 10000 --seed 42 -v /mnt/famfs/shared_queue
```

### 5.5 Expected Output

**Host A (Producer):**
```
pcq:    /mnt/famfs/shared_queue
pcq producer: nsent=10000 nerrors=0 nfull=0
pcq consumer: nreceived=0 nerrors=0 nempty=0 retries=0
```

**Host B (Consumer):**
```
pcq:    /mnt/famfs/shared_queue
pcq producer: nsent=0 nerrors=0 nfull=0
pcq consumer: nreceived=10000 nerrors=0 nempty=X retries=Y
```

Note: `retries` indicates cache coherency events where the consumer had to invalidate stale cache lines.

### 5.6 Monitoring with Status Updates

For long-running tests, use status updates:

**Host B (Consumer):**
```bash
pcq --consumer --time 60 --status 5 /mnt/famfs/shared_queue
```

**Host A (Producer):**
```bash
pcq --producer --time 60 --status 5 /mnt/famfs/shared_queue
```

### 5.7 Troubleshooting Two-Host Setup

| Issue | Possible Cause | Solution |
|-------|----------------|----------|
| Consumer can't open queue | Permission not set for consumer | Run `pcq --setperm c` or `--setperm b` |
| Producer can't open queue | Permission not set for producer | Run `pcq --setperm p` or `--setperm b` |
| Queue not visible on Host B | Log not replayed | Run `famfs logplay /mnt/famfs` |
| CRC errors | Cache coherency failure | Check memory fabric connectivity |
| Sequence mismatch | Multiple consumers or data corruption | Ensure single consumer; check memory |

---

## 6. Challenges with Cache Coherency and PCQ

### 6.1 The Cache Coherency Problem

When multiple hosts share memory through fabric-attached memory (FAM), there is no hardware cache coherency between hosts. Each CPU maintains its own cache hierarchy, and changes made by one host may not be visible to another host due to:

1. **Stale cache lines**: Host B may have cached old data that Host A has since updated
2. **Write buffering**: Host A's writes may not have reached main memory yet
3. **Cache line granularity**: Partial updates within a cache line may cause inconsistency

### 6.2 How PCQ Addresses Cache Coherency

PCQ implements a software-based cache coherency protocol using explicit cache management instructions:

#### Producer Side (After Writing)

```c
// After writing data to the bucket
flush_processor_cache(bucket_addr, pcq->bucket_size);

// After updating producer_index
flush_processor_cache(&pcq->producer_index, sizeof(pcq->producer_index));
```

The `flush_processor_cache()` function:
- Uses `CLWB` (Cache Line Write Back) on x86 when available
- Falls back to `CLFLUSHOPT` or `CLFLUSH` on older processors
- Ensures data is written to main memory where other hosts can see it

#### Consumer Side (Before Reading)

```c
// Before checking producer_index
invalidate_processor_cache(&pcq->producer_index, sizeof(pcq->producer_index));

// If CRC is bad, invalidate and retry
invalidate_processor_cache(bucket_addr, pcq->bucket_size);
```

The `invalidate_processor_cache()` function:
- Uses `CLFLUSHOPT` or `CLFLUSH` to invalidate cache lines
- Forces subsequent reads to fetch from main memory

### 6.3 The Retry Mechanism

PCQ implements a retry mechanism for handling transient cache coherency failures:
The `retries` counter in PCQ output indicates how often this mechanism was triggered.

### 6.4 Memory Barriers and Ordering

PCQ uses memory barriers (SFENCE on x86) to ensure ordering:

```c
// In flush_processor_cache:
x86_flush_range((uintptr_t)addr, len, flush_cacheline_func);
fence_func();  // SFENCE - ensures all flushes complete
```

This ensures:
- All cache line flushes complete before the function returns
- Subsequent operations see the flushed state

### 6.5 Structure Padding for Cache Line Separation

PCQ uses strategic padding to avoid false sharing:

```c
struct pcq {
    u64 producer_index;
    char pad[1024];      // Separates producer_index from next_seq
    u64 next_seq;
};

struct pcq_consumer {
    u64 consumer_index;
    char pad2[1048576];  // 1MB padding
    u64 next_seq;
};
```

This ensures that:
- Fields updated by different parties are on different cache lines
- Flush/invalidate operations don't cause unnecessary coherency traffic

### 6.6 Known Challenges and Limitations

#### Challenge 1: Performance Overhead

Cache flush and invalidate operations are expensive:
- Each `CLFLUSH`/`CLFLUSHOPT` takes 50-200 CPU cycles
- Memory barriers add additional latency
- Round-trip to main memory is much slower than cache access

**Mitigation**: PCQ flushes only the modified cache lines, not the entire data structure.

#### Challenge 2: Retry Storms

Under high load or with memory latency issues, consumers may experience many retries:

```
pcq consumer: nreceived=10000 nerrors=0 nempty=50 retries=500
```

**Diagnosis**: High retry count indicates cache coherency stress.

**Mitigation**:
- Use larger bucket sizes to amortize coherency overhead
- Reduce message rate
- Check memory fabric health

#### Challenge 3: ABA Problem

The sequence number helps detect the ABA problem where:
1. Consumer reads position N
2. Producer wraps around and writes new data at position N
3. Consumer thinks it has the original data

PCQ's sequence numbers make this detectable as a sequence mismatch.

#### Challenge 4: Memory Ordering on Different Architectures

Different CPU architectures have different memory models:
- x86: Total Store Order (TSO) - relatively strong
- ARM64: Weakly ordered - requires more barriers
- RISC-V: RVWMO - weak ordering

The `libfcc` library provides architecture-specific implementations to handle these differences.

### 6.7 Debugging Cache Coherency Issues

#### Enable Verbose Mode

```bash
pcq --consumer -v -v --nmessages 100 /mnt/famfs/myqueue
```

Double `-v` shows cache coherency details:
```
pcq_consumer_get: bucket=42 seq=42
pcq_consumer_get: bucket_size=1024 seq_offset=1008 crc_offset=1016 crc deadbeef/deadbeef
```

#### Disable Flush Operations (Testing Only)

For debugging, you can disable cache flush/invalidate to isolate issues:

```bash
pcq -pc --dontflush --nmessages 100 /mnt/famfs/myqueue
```

**Warning**: This will likely cause CRC errors in multi-host scenarios but can help identify if issues are cache-related.

#### Monitor Retry Statistics

Track retries over time:

```bash
pcq --consumer --time 60 --status 1 /mnt/famfs/shared_queue 2>&1 | grep retries
```

A steadily increasing retry count may indicate:
- Memory fabric congestion
- Hardware issues
- Insufficient flush operations in the producer

### 6.8 Best Practices for Cache Coherency

1. **Always flush after writes**: Any data intended to be read by another host must be flushed
2. **Invalidate before reads**: Assume your cache is stale for shared data
3. **Use sequence numbers**: Detect ABA problems and reordering
4. **Use CRC or checksums**: Verify data integrity
5. **Design for retries**: Expect occasional coherency delays
6. **Separate hot fields**: Use padding to avoid false sharing
7. **Batch operations**: Amortize flush overhead over multiple items when possible
8. **Test with multiple hosts**: Single-host testing may hide coherency bugs

---

## Appendix: Command Reference

### Create a Queue

```bash
pcq --create --bsize <size> --nbuckets <count> [-u <uid>] [-g <gid>] <queuename>
```

### Run Producer

```bash
pcq --producer [-N <messages>] [--time <seconds>] [--seed <seed>] [-v] <queuename>
```

### Run Consumer

```bash
pcq --consumer [-N <messages>] [--time <seconds>] [--seed <seed>] [-v] <queuename>
```

### Run Both Together

```bash
pcq -pc [-N <messages>] [--time <seconds>] [--seed <seed>] [-s <status_interval>] <queuename>
```

### Queue Management

```bash
pcq --info <queuename>           # Show queue state
pcq --drain <queuename>          # Empty the queue
pcq --setperm <p|c|b|n> <queuename>  # Set permissions
```

### Options Summary

| Option | Description |
|--------|-------------|
| `-C, --create` | Create a new queue |
| `-b, --bsize <size>` | Bucket size (must be power of 2) |
| `-n, --nbuckets <count>` | Number of buckets |
| `-p, --producer` | Run as producer |
| `-c, --consumer` | Run as consumer |
| `-N, --nmessages <n>` | Number of messages |
| `-t, --time <seconds>` | Run duration |
| `-S, --seed <seed>` | Random seed for payload |
| `-s, --status <interval>` | Status update interval |
| `-d, --drain` | Drain queue to empty |
| `-i, --info` | Show queue information |
| `-P, --setperm <p\|c\|b\|n>` | Set permissions |
| `-D, --dontflush` | Disable cache flush (testing) |
| `-v` | Verbose output (use twice for more) |
| `-f, --statusfile <file>` | Write status to file |
