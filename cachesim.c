/**
 * @file cachesim.c
 * @brief Memory cache simulator in C
 *
 * A cache is a higher-speed data storage layer which stores a subset of
 * data from a larger, slower-speed data storage layer.
 * Caches are comprised of a number of sets, each with a number of lines.
 *
 * When we load from a cache, we search for a line with a desired tag,
 * and load data at a desired block offset. If no line is found, we load
 * data from memory and store it on the cache.
 * When we store to a cache, we search for an empty line, or a line
 * matching our replacement policy, and write in the data.
 *
 * Hit: data with desired tag found within cache.
 * Miss: data with desired tag not found in cache.
 * Eviction: data removed in order to write new data.
 * Dirty bits: bits stored in cache not yet stored in memory.
 *
 * How it works:
 *     1. Reads, validates, executes command line instructions.
 *     2. Creates queue of trace instructions from validated trace file.
 *     3. Makes cache and performs trace instructions on cache while
 *        storing results.
 *     4. Returns results of trace instructions.
 *
 * To get started run from command line: ./csim -h
 *
 * @author Iltikin Wayet
 */

#include "cachelab.h"
#include <errno.h>
#include <getopt.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

/**
 * @brief Data structure to hold cache basic information.
 *     Information defined by user.
 *
 * @arg s       : number of set index bits
 * @arg E       : number of lines per set
 * @arg b       : number of block bits
 * @arg set_num : number of sets
 * @arg v_flag  : verbose flag (true when -v option given)
 */
typedef struct cache_info_struct *cache_info;
struct cache_info_struct {
    unsigned long int s;
    unsigned long int E;
    unsigned long int b;
    unsigned long int set_num;
    bool v_flag;
};

/**
 * @brief Queue of trace operations.
 * First operation added to queue first to be executed.
 *
 * @arg address : memory address to be accessed
 * @arg size    : number of bytes to be accessed
 * @arg store   : type of operation; true if 'S', false 'L'
 * @arg next    : pointer to next operation in queue
 */
typedef struct trace_struct *traces;
struct trace_struct {
    unsigned long long address;
    unsigned long long size;
    traces next;
    bool store;
};

/**
 * @brief Basic unit of cache memory.
 *
 * @arg tag      : tag value of the line
 * @arg order    : recency of line usage (greater is more recent)
 * @arg is_valid : cache line valid
 * @arg has_data : cache line not empty
 */
typedef struct block_struct *block;
struct block_struct {
    unsigned long long tag;
    unsigned long long order;
    bool is_valid;
    bool has_data;
};

void help_msg(void) {
    printf("Usage:  ./csim -ref [-v] -s <s> -E <E> -b <b> -t <trace>\n"
           "        ./csim -ref -h\n\n"
           "    -h            Print this help message and exit\n"
           "    -v            Verbose mode: report effects of each memory "
           "operation\n"
           "    -s <s>        Number of set index bits (there are 2**s sets)\n"
           "    -b <b>        Number of block bits (there are 2**b blocks)\n"
           "    -E <E>        Number of lines per set (associativity)\n"
           "    -t <trace>    File name of the memory trace to process\n\n"
           "The -s, -b, -E, and -t options must be supplied for all "
           "simulations.\n");
}

/**
 * @brief Validates strtoul output.
 *
 * @param[in] x   : output value to be checked
 * @param[in] msg : error message to be printed if x invalid
 *
 * @return 1 : if invalid strtoul output
 * @return 0 : if valid strtoul output
 */
int check_strtoul(unsigned long int x, const char *msg) {
    int r = 0;
    if ((errno == ERANGE && x == ULONG_MAX) || (errno != 0 && x == 0)) {
        perror("strtoul");
        r = 1;
    }
    if ((signed long int)x < 0)
        r = 1;
    if (r == 1)
        fprintf(stderr, "%s\n", msg);
    return r;
}

/**
 * @brief Parses and validates trace file.
 *     Creates queue of trace operations.
 *
 * @param[in] trace_file : text path of trace file
 * @param[in] result     : value modified if errors
 *     0 if no errors
 *     1 if trace format error
 *     2 if memory error during trace allocation.
 *
 * @return NULL  : if memory error in allocating trace
 * @return trace : otherwise
 *     Allocating queue items may result in memory error.
 *     Thus, must check result integer for error flags.
 */
traces make_trace(const char *trace_file, int *result) {
    const int LINELEN = 40;

    FILE *tfp = fopen(trace_file, "rt");
    if (!tfp) {
        fprintf(stderr, "Error opening '%s': %s\n", trace_file,
                strerror(errno));
        *result = 1;
        return NULL;
    }

    char linebuf[LINELEN];

    traces trace = malloc(sizeof(struct trace_struct));
    if (trace == NULL) {
        *result = 2;
        return NULL;
    }
    traces curr = trace;

    while (fgets(linebuf, LINELEN, tfp) != NULL) {

        if (linebuf[1] != ' ' || linebuf[2] == ' ' ||
            (linebuf[0] != 'S' && linebuf[0] != 'L') ||
            strlen(&linebuf[0]) < 5) {
            fprintf(stderr, "Invalid trace format\n");
            *result = 1;
            return trace;
        }
        bool op = (linebuf[0] == 'S');

        char *tok = strtok(&linebuf[1], ",");
        errno = 0;
        unsigned long long ad = strtoul(tok, NULL, 16);
        if (check_strtoul(ad, "Invalid address input.") == 1) {
            *result = 1;
            return trace;
        }

        tok = strtok(NULL, ",");
        errno = 0;
        unsigned long long sz = strtoul(tok, NULL, 0);
        if (check_strtoul(sz, "Invalid size input.") == 1) {
            *result = 1;
            return trace;
        }

        traces temp = malloc(sizeof(struct trace_struct));
        if (temp == NULL) {
            *result = 2;
            return trace;
        }
        temp->address = ad;
        temp->size = sz;
        temp->store = op;
        curr->next = temp;
        curr = temp;
    }
    traces temp = trace;
    trace = trace->next;
    free(temp);

    fclose(tfp);
    return trace;
}

void trace_free(traces trace) {
    traces curr = trace;
    while (curr != NULL) {
        trace = curr->next;
        free(curr);
        curr = trace;
    }
}

/**
 * @brief Allocates matrix representation of cache.
 *     Block is the smallest unit of the matrix (correct term is line).
 *
 * @param[in] info   : struct containing cache info defined by user
 *     Cache to be made according to these specifications.
 * @param[in] result : value modified if errors
 *     0 if no errors
 *     1 if memory error during allocation
 *
 * @return allocated cache
 *     Returns cache even if memory error partially through allocation.
 *     Thus must be sure to check result integer value.
 * @return NULL if memory error with initial allocate
 */
block **make_cache(cache_info info, int *result) {
    unsigned long int set_num = info->set_num;
    unsigned long int E = info->E;

    block **ret_val = calloc(sizeof(block *), set_num);
    if (ret_val == NULL) {
        *result = 1;
        return NULL;
    }

    for (unsigned long int i = 0; i < set_num; i++) {
        ret_val[i] = calloc(sizeof(block), E);
        if (ret_val[i] == NULL) {
            *result = 1;
            return ret_val;
        }

        for (unsigned long int j = 0; j < E; j++) {
            ret_val[i][j] = calloc(sizeof(struct block_struct), 1);
            if (ret_val[i][j] == NULL) {
                *result = 1;
                return ret_val;
            }
        }
    }
    return ret_val;
}

void cache_free(cache_info info, block **cache) {
    unsigned long int set_num = info->set_num;
    unsigned long int E = info->E;
    for (unsigned long int i = 0; i < set_num; i++) {
        for (unsigned long int j = 0; j < E; j++)
            free(cache[i][j]);
        free(cache[i]);
    }
    free(cache);
}

/**
 * @brief Simulates cache behavior.
 *     With every trace operation:
 *         1. Checks for hit
 *         2. If miss, checks for eviction
 *         3. Prints if verbose flag raised
 *
 * @param[in] info   : struct containing cache info defined by user
 * @param[in] trace  : queue of trace operations executed by function
 * @param[in] cache  : 2d matrix representation of cache
 * @param[in] result : value modified if errors
 *     0 if no errors
 *     2 if memory error
 *
 * @return NULL if memory allocation error
 * @return struct of statistics from simulation
 *     Counts of hits, misses, evictions, dirty bytes, dirty evictions
 */
csim_stats_t *simulator(cache_info info, traces trace, block **cache,
                        int *result) {

    unsigned long int s = info->s;
    unsigned long int E = info->E;
    unsigned long int b = info->b;

    unsigned long long set_index;
    unsigned long long tag_value;
    unsigned long long trace_num = 0;

    csim_stats_t *ret_val = calloc(sizeof(csim_stats_t), 1);
    if (ret_val == NULL) {
        *result = 2;
        return NULL;
    }

    traces curr = trace;
    while (curr != NULL) {
        set_index = (curr->address & (~(~0ULL << s)) << b) >> b;
        tag_value = curr->address >> (s + b);

        block *set = cache[set_index];

        bool hit = false;
        unsigned long int hit_line;
        bool empty = false;
        unsigned long int mpt_line;

        // looks for line with tag
        for (unsigned long int i = 0; !hit && i < E; i++) {
            if (set[i]->tag == tag_value && set[i]->is_valid)
                hit = true;
            if (hit)
                hit_line = i;
        }

        if (hit) {
            if (curr->store && !(set[hit_line]->has_data)) {
                ret_val->dirty_bytes += 1UL << b;
                set[hit_line]->has_data = true;
            }
            set[hit_line]->order = trace_num;
            ret_val->hits++;
        } else { // then we have a miss
            ret_val->misses++;

            for (unsigned long int i = 0; !empty && i < E; i++) {
                if (!(set[i]->is_valid))
                    empty = true;
                if (empty)
                    mpt_line = i;
            }

            if (empty) {
                set[mpt_line]->tag = tag_value;
                set[mpt_line]->is_valid = true;
                set[mpt_line]->order = trace_num;
                set[mpt_line]->has_data = false;
                if (curr->store && !(set[mpt_line]->has_data)) {
                    ret_val->dirty_bytes += 1UL << b;
                    set[mpt_line]->has_data = true;
                }
            } else {
                ret_val->evictions++;

                unsigned long int least_used = 0;
                for (unsigned long int i = 0; i < E; i++) {
                    if ((set[i]->order) < (set[least_used]->order))
                        least_used = i;
                }

                if (set[least_used]->has_data) {
                    ret_val->dirty_evictions += 1UL << b;
                    ret_val->dirty_bytes -= 1UL << b;
                }

                set[least_used]->tag = tag_value;
                set[least_used]->order = trace_num;
                set[least_used]->has_data = false;
                if (curr->store && !(set[least_used]->has_data)) {
                    ret_val->dirty_bytes += 1UL << b;
                    set[least_used]->has_data = true;
                }
            }
        }

        if (info->v_flag) {
            char op = curr->store ? 'S' : 'L';
            printf("%c %llu,%llu ", op, curr->address, curr->size);
            if (!hit) {
                printf("miss ");
                if (!empty)
                    printf("eviction ");
            } else
                printf("hit ");
            printf("\n");
        }
        curr = curr->next;
        trace_num++;
    }
    return ret_val;
}

/**
 * @brief Parses command line arguments, executes simulation accordingly.
 *     Initially stores all info into cache_info.
 *     Then creates trace, cache, simulation in order.
 *
 * @param[in] argc : Length of command line input
 * @param[in] argv : Command line input string
 *
 * @return 1 if errors in execution.
 * @return 0 if no errors in execution.
 */
int main(int argc, char **argv) {
    cache_info info = malloc(sizeof(struct cache_info_struct));

    info->s = 0;
    info->E = 0;
    info->b = 0;
    info->v_flag = false;

    bool h_flag = false;
    int opt_n = 0;
    int opt;
    char *filename;

    do {
        opt = getopt(argc, argv, "hvs:b:E:t:");
        switch (opt) {
        case 'h':
            h_flag = true;
            opt = -1;
            break;
        case 'v':
            info->v_flag = true;
            break;
        case 's':
            errno = 0;
            info->s = strtoul(optarg, NULL, 0);
            if (check_strtoul(info->s, "Invalid option argument -- 's'") == 1)
                return 1;
            opt_n++;
            break;
        case 'E':
            errno = 0;
            info->E = strtoul(optarg, NULL, 0);
            if (check_strtoul(info->E, "Invalid option argument -- 'E'") == 1 ||
                (signed long int)info->E == 0)
                return 1;
            opt_n++;
            break;
        case 'b':
            errno = 0;
            info->b = strtoul(optarg, NULL, 0);
            if (check_strtoul(info->b, "Invalid option argument -- 'b'") == 1)
                return 1;
            opt_n++;
            break;
        case 't':
            filename = optarg;
            opt_n++;
            break;
        default:
            opt = -1;
        }
    } while (opt != -1);

    if (h_flag) {
        printf("csim.c info:\n");
        help_msg();
        return 1;
    }
    if (opt_n != 4) {
        printf("Mandatory arguments missing or zero.\n");
        help_msg();
        return 1;
    }
    if (info->s + info->b > 64) {
        fprintf(stderr, "Arguments s, b represent > 64 addressable bits.\n");
        return 1;
    }

    info->set_num = 1UL << (info->s);

    // Above this point parsing command line into cache_info. Below simulation

    int trace_result = 0;
    traces trace = make_trace(filename, &trace_result);
    if (trace_result != 0) {
        if (trace_result == 1)
            fprintf(stderr, "Error in trace file -- %s\n", filename);
        else // trace_result == 2
            fprintf(stderr, "Memory error when allocating trace.");
        if (trace != NULL)
            trace_free(trace);
        return 1;
    }

    int cache_result = 0;
    block **cache = make_cache(info, &cache_result);
    if (cache_result != 0) {
        fprintf(stderr, "Memory error when allocating cache.");
        if (cache != NULL)
            cache_free(info, cache);
        return 1;
    }

    int simulator_result = 0;
    csim_stats_t *simulated = simulator(info, trace, cache, &simulator_result);
    if (simulator_result != 0) {
        fprintf(stderr, "Memory error when running simulator.");
        return 1;
    }
    printSummary(simulated);

    trace_free(trace);
    cache_free(info, cache);
    free(info);
    free(simulated);

    return 0;
}
