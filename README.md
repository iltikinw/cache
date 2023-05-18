# Cachesim.c - a cache memory simulator built in C.
Written as part of CMU's Computer Systems course. Received a 95.3% grade.
## Info on caches
A cache is a higher-speed data storage layer which stores a subset of data from a larger, slower-speed data storage layer. Caches are comprised of a number of sets, each with a number of lines.
When we load from a cache, we search for a line with a desired tag, and load data at a desired block offset. If no line is found, we load data from memory and store it on the cache. When we store to a cache, we search for an empty line, or a line matching our replacement policy, and write in the data.
* **Hit:** data with desired tag found within cache.
* **Miss:** data with desired tag not found in cache.
* **Eviction:** data removed in order to write new data.
* **Dirty bits:** bits stored in cache not yet stored in memory.
## How the simulator works
The cache simulator takes as input a trace file, the number of set index bits, the number of block bits, and the number of lines per set. Then, it outputs the number of hits, misses, and evicts from running the trace file.
### High-level overview
1. Reads, validates, executes command line instructions.
2. Creates queue of trace instructions from validated trace file.
3. Makes cache and performs trace instructions on cache while storing results.
4. Returns results of trace instructions.
## Trace file format
cachesim.c expects traces to be text files with one memory operation per line. Each line must be in the format:
```c Op Addr,Size```
* ```Op``` denotes the type of memory access. It can be either L for a load, or S for a store.
* ```Addr``` gives the memory address to be accessed. It should be a 64-bit hexadecimal number, without a leading 0x.
* ```Size``` gives the number of bytes to be accessed at Addr. It should be a small, positive decimal number
## Demos
The version publicly available in this repository does not work on its own. For demos, please contact me at iltikinw@gmail.com, and I'd love to connect!
To get started, compile and run from command line: ```./csim -h```