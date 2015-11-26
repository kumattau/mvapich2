#define BENCHMARK "OSU MPI%s Multiple Bandwidth / Message Rate Test"
/*
 * Copyright (C) 2002-2015 the Network-Based Computing Laboratory
 * (NBCL), The Ohio State University. 
 *
 * Contact: Dr. D. K. Panda (panda@cse.ohio-state.edu)
 *
 * For detailed copyright and licensing information, please refer to the
 * copyright file COPYRIGHT in the top level OMB directory.
 */

#include <mpi.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>

#ifdef _ENABLE_OPENACC_
#include <openacc.h>
#endif

#ifdef _ENABLE_CUDA_
#include <cuda.h>
#include <cuda_runtime.h>
#endif

#define DEFAULT_WINDOW       (64)

#define ITERS_SMALL          (100)          
#define WARMUP_ITERS_SMALL   (10)
#define ITERS_LARGE          (20)
#define WARMUP_ITERS_LARGE   (2)
#define LARGE_THRESHOLD      (8192)

#define WINDOW_SIZES {8, 16, 32, 64, 128}
#define WINDOW_SIZES_COUNT   (5)

#define MAX_MSG_SIZE         (1<<22)

#ifdef _ENABLE_OPENACC_
#   define OPENACC_ENABLED 1
#else
#   define OPENACC_ENABLED 0
#endif

#ifdef _ENABLE_CUDA_
#   define CUDA_ENABLED 1
#else
#   define CUDA_ENABLED 0
#endif

MPI_Request * request;
MPI_Status * reqstat;

#ifdef _ENABLE_CUDA_
CUcontext cuContext;
#endif

int pairs = 0;
int print_rate = 1;
int window_size = DEFAULT_WINDOW;
int window_varied = 0;

enum po_ret_type {
    po_cuda_not_avail,
    po_openacc_not_avail,
    po_bad_usage,
    po_help_message,
    po_okay,
};

enum accel_type {
    none,
    cuda,
    openacc
};

struct {
    char src;
    char dst;
    enum accel_type accel;
} options;

void usage (void);
int process_options (int argc, char *argv[]);
int allocate_memory (char **sbuf, char **rbuf);
void touch_data (void *sbuf, void *rbuf, int rank, size_t size);
void free_memory (void *sbuf, void *rbuf);
int init_accel (void);
int cleanup_accel (void);

double calc_bw(int rank, int size, int num_pairs, int window_size, char *s_buf, char *r_buf);
void usage();

#ifdef PACKAGE_VERSION
#   define HEADER "# " BENCHMARK " v" PACKAGE_VERSION "\n"
#else
#   define HEADER "# " BENCHMARK "\n"
#endif

MPI_Request * request;
MPI_Status * reqstat;

double calc_bw(int rank, int size, int num_pairs, int window_size, char *s_buf, char *r_buf);
void usage();

static int loop;
static int skip;
static int loop_override;
static int skip_override;

int main(int argc, char *argv[])
{
    char *s_buf, *r_buf;
    int numprocs, rank;
    int c, curr_size;
    int po_ret = process_options(argc, argv);

    if (po_okay == po_ret && none != options.accel) {
        if (init_accel()) {
            fprintf(stderr, "Error initializing device\n");
            exit(EXIT_FAILURE);
        }
    }

    MPI_Init(&argc, &argv);
    MPI_Comm_size(MPI_COMM_WORLD, &numprocs);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);

    if (0 == rank) {
        switch (po_ret) {
            case po_cuda_not_avail:
                fprintf(stderr, "CUDA support not enabled.  Please recompile "
                        "benchmark with CUDA support.\n");
                break;
            case po_openacc_not_avail:
                fprintf(stderr, "OPENACC support not enabled.  Please "
                        "recompile benchmark with OPENACC support.\n");
                break;
            case po_bad_usage:
            case po_help_message:
                usage();
                break;
        }
    }

    switch (po_ret) {
        case po_cuda_not_avail:
        case po_openacc_not_avail:
        case po_bad_usage:
            MPI_Finalize();
            exit(EXIT_FAILURE);
        case po_help_message:
            MPI_Finalize();
            exit(EXIT_SUCCESS);
        case po_okay:
            break;
    }

    pairs            = (pairs == 0 || pairs > numprocs>>1) ? numprocs>>1 : pairs;

    if(numprocs < 2) {
        if(rank == 0) {
            fprintf(stderr, "This test requires at least two processes\n");
        }

        MPI_Finalize();

        return EXIT_FAILURE;
    }

    if (allocate_memory(&s_buf, &r_buf)) {
        /* Error allocating memory */
        MPI_Finalize();
        exit(EXIT_FAILURE);
    }

    if(rank == 0) {
        switch (options.accel) {
            case cuda:
                fprintf(stdout, HEADER, "-CUDA");
                break;
            case openacc:
                fprintf(stdout, HEADER, "-OPENACC");
                break;
            default:
                fprintf(stdout, HEADER, "");
                break;
        }

        if(window_varied) {
            fprintf(stdout, "# [ pairs: %d ] [ window size: varied ]\n", pairs);
            fprintf(stdout, "\n# Uni-directional Bandwidth (MB/sec)\n");
        }

        else {
            fprintf(stdout, "# [ pairs: %d ] [ window size: %d ]\n", pairs,
                    window_size);

            if(print_rate) {
                fprintf(stdout, "%-*s%*s%*s\n", 10, "# Size", FIELD_WIDTH,
                        "MB/s", FIELD_WIDTH, "Messages/s");
            }

            else {
                fprintf(stdout, "%-*s%*s\n", 10, "# Size", FIELD_WIDTH, "MB/s");
            }
        }

        fflush(stdout);
    }

   /* More than one window size */

   if(window_varied) {
       int window_array[] = WINDOW_SIZES;
       double ** bandwidth_results;
       int log_val = 1, tmp_message_size = MAX_MSG_SIZE;
       int i, j;

       for(i = 0; i < WINDOW_SIZES_COUNT; i++) {
           if(window_array[i] > window_size) {
               window_size = window_array[i];
           }
       }

       request = (MPI_Request *) malloc(sizeof(MPI_Request) * window_size);
       reqstat = (MPI_Status *) malloc(sizeof(MPI_Status) * window_size);

       while(tmp_message_size >>= 1) {
           log_val++;
       }

       bandwidth_results = (double **) malloc(sizeof(double *) * log_val);

       for(i = 0; i < log_val; i++) {
           bandwidth_results[i] = (double *)malloc(sizeof(double) *
                   WINDOW_SIZES_COUNT);
       }

       if(rank == 0) {
           fprintf(stdout, "#      ");

           for(i = 0; i < WINDOW_SIZES_COUNT; i++) {
               fprintf(stdout, "  %10d", window_array[i]);
           }

           fprintf(stdout, "\n");
           fflush(stdout);
       }
    
       for(j = 0, curr_size = 1; curr_size <= MAX_MSG_SIZE; curr_size *= 2, j++) {
           if(rank == 0) {
               fprintf(stdout, "%-7d", curr_size);
           }

           for(i = 0; i < WINDOW_SIZES_COUNT; i++) {
               bandwidth_results[j][i] = calc_bw(rank, curr_size, pairs,
                       window_array[i], s_buf, r_buf);

               if(rank == 0) {
                   fprintf(stdout, "  %10.*f", FLOAT_PRECISION,
                           bandwidth_results[j][i]);
               }
           }

           if(rank == 0) {
               fprintf(stdout, "\n");
               fflush(stdout);
           }
       }

       if(rank == 0 && print_rate) {
            fprintf(stdout, "\n# Message Rate Profile\n");
            fprintf(stdout, "#      ");

            for(i = 0; i < WINDOW_SIZES_COUNT; i++) {
                fprintf(stdout, "  %10d", window_array[i]);
            }       

            fprintf(stdout, "\n");
            fflush(stdout);

            for(c = 0, curr_size = 1; curr_size <= MAX_MSG_SIZE; curr_size *= 2) { 
                fprintf(stdout, "%-7d", curr_size); 

                for(i = 0; i < WINDOW_SIZES_COUNT; i++) {
                    double rate = 1e6 * bandwidth_results[c][i] / curr_size;

                    fprintf(stdout, "  %10.2f", rate);
                }       

                fprintf(stdout, "\n");
                fflush(stdout);
                c++;    
            }
       }
   }

   else {
       /* Just one window size */
       request = (MPI_Request *)malloc(sizeof(MPI_Request) * window_size);
       reqstat = (MPI_Status *)malloc(sizeof(MPI_Status) * window_size);

       for(curr_size = 1; curr_size <= MAX_MSG_SIZE; curr_size *= 2) {
           double bw, rate;

           bw = calc_bw(rank, curr_size, pairs, window_size, s_buf, r_buf);

           if(rank == 0) {
               rate = 1e6 * bw / curr_size;

               if(print_rate) {
                   fprintf(stdout, "%-*d%*.*f%*.*f\n", 10, curr_size,
                           FIELD_WIDTH, FLOAT_PRECISION, bw, FIELD_WIDTH,
                           FLOAT_PRECISION, rate);
               }

               else {
                   fprintf(stdout, "%-*d%*.*f\n", 10, curr_size, FIELD_WIDTH,
                           FLOAT_PRECISION, bw);
               }
           } 
       }
   }

   free_memory(s_buf, r_buf);
   MPI_Finalize();

   if (none != options.accel) {
       if (cleanup_accel()) {
           fprintf(stderr, "Error cleaning up device\n");
           exit(EXIT_FAILURE);
       }
   }

   return EXIT_SUCCESS;
}

double 
calc_bw(int rank, int size, int num_pairs, int window_size, char *s_buf,
        char *r_buf)
{
    double t_start = 0, t_end = 0, t = 0, sum_time = 0, bw = 0;
    int i, j, target;
    int mult = (DEFAULT_WINDOW / window_size) > 0 ? (DEFAULT_WINDOW /
            window_size) : 1;

    touch_data(s_buf, r_buf, rank, size);

    if (size > LARGE_THRESHOLD) {
        if (!loop_override) {
            loop = ITERS_LARGE * mult;
        }

        if (!skip_override) {
            skip = WARMUP_ITERS_LARGE * mult;
        }
    }

    else {
        if (!loop_override) {
            loop = ITERS_SMALL * mult;
        }

        if (!skip_override) {
            skip = WARMUP_ITERS_SMALL * mult;
        }
    }
    
    MPI_Barrier(MPI_COMM_WORLD);

    if(rank < num_pairs) {
        target = rank + num_pairs;

        for(i = 0; i <  loop +  skip; i++) {
            if(i ==  skip) {
                MPI_Barrier(MPI_COMM_WORLD);
                t_start = MPI_Wtime();
            }

            for(j = 0; j < window_size; j++) {
                MPI_Isend(s_buf, size, MPI_CHAR, target, 100, MPI_COMM_WORLD,
                        request + j);
            }

            MPI_Waitall(window_size, request, reqstat);
            MPI_Recv(r_buf, 4, MPI_CHAR, target, 101, MPI_COMM_WORLD,
                    &reqstat[0]);
        }

        t_end = MPI_Wtime();
        t = t_end - t_start;
    }

    else if(rank < num_pairs * 2) {
        target = rank - num_pairs;

        for(i = 0; i <  loop +  skip; i++) {
            if(i ==  skip) {
                MPI_Barrier(MPI_COMM_WORLD);
            }

            for(j = 0; j < window_size; j++) {
                MPI_Irecv(r_buf, size, MPI_CHAR, target, 100, MPI_COMM_WORLD,
                        request + j);
            }

            MPI_Waitall(window_size, request, reqstat);
            MPI_Send(s_buf, 4, MPI_CHAR, target, 101, MPI_COMM_WORLD);
        }
    }

    else {
        MPI_Barrier(MPI_COMM_WORLD);
    }

    MPI_Reduce(&t, &sum_time, 1, MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);

    if(rank == 0) {
        double tmp = size / 1e6 * num_pairs ;
        
        sum_time /= num_pairs;
        tmp = tmp *  loop * window_size;
        bw = tmp / sum_time;

        return bw;
    }

    return 0;
}


int
process_options (int argc, char *argv[])
{
    int c;
    extern char * optarg;
    extern int optind;

    char const * optstring = (CUDA_ENABLED || OPENACC_ENABLED) ? "+d:p:w:r:x:i:vh" : "+p:w:r:x:i:vh";

    /*allocate buffers on host by default*/
    options.accel = none;

    loop_override = 0;
    skip_override = 0;

    while((c = getopt(argc, argv, optstring)) != -1) {
        switch (c) {
            case 'i':
                loop = atoi(optarg);
                loop_override = 1;
                break;
            case 'x':
                skip = atoi(optarg);
                skip_override = 1;
                break;
            case 'p':
                pairs = atoi(optarg);
                
                if (pairs <= 0) 
                    return po_bad_usage;

                break;

            case 'w':
                window_size = atoi(optarg);

                break;

            case 'v':
                window_varied = 1;
                break;

            case 'r':
                print_rate = atoi(optarg);

                if(0 != print_rate && 1 != print_rate) 
                    return po_bad_usage;

                break;

            case 'd':
                /* optarg should contain cuda or openacc */
                if (0 == strncasecmp(optarg, "cuda", 10)) {
                    if (!CUDA_ENABLED) {
                        return po_cuda_not_avail;
                    }
                    options.accel = cuda;
                }
                else if (0 == strncasecmp(optarg, "openacc", 10)) {
                    if (!OPENACC_ENABLED) {
                        return po_openacc_not_avail;
                    }
                    options.accel = openacc;
                }
                else {
                    return po_bad_usage;
                }

                break;

            case 'h':
                return po_help_message;
                        
                break; 

            default:

                return po_bad_usage;
        }
    }

    return po_okay;
}

void 
usage() 
{
    printf("Usage: osu_mbw_mr [options]\n\n");

    printf("Options:\n");
    printf("  -r=<0,1>         Print uni-directional message rate (default 1)\n");
    printf("  -p=<pairs>       Number of pairs involved (default np / 2)\n");
    printf("  -w=<window>      Number of messages sent before acknowledgement (64, 10)\n");
    printf("                   [cannot be used with -v]\n");
    printf("  -v               Vary the window size (default no)\n");
    printf("                   [cannot be used with -w]\n");
    if (CUDA_ENABLED || OPENACC_ENABLED) {
        printf("  -d TYPE          Accelerator device buffers can be of TYPE "
                "`cuda' or `openacc'\n");
    }
    printf("  -h               Print this help\n");
    printf("\n");
    printf("  Note: This benchmark relies on block ordering of the ranks.  Please see\n");
    printf("        the README for more information.\n");
    fflush(stdout);
}


int
init_accel (void)
{
#if defined(_ENABLE_OPENACC_) || defined(_ENABLE_CUDA_)
     char * str;
     int local_rank, dev_count;
     int dev_id = 0;
#endif
#ifdef _ENABLE_CUDA_
     CUresult curesult = CUDA_SUCCESS;
     CUdevice cuDevice;
#endif

     switch (options.accel) {
#ifdef _ENABLE_CUDA_
        case cuda:
            if ((str = getenv("LOCAL_RANK")) != NULL) {
                cudaGetDeviceCount(&dev_count);
                local_rank = atoi(str);
                dev_id = local_rank % dev_count;
            }

            curesult = cuInit(0);
            if (curesult != CUDA_SUCCESS) {
                return 1;
            }

            curesult = cuDeviceGet(&cuDevice, dev_id);
            if (curesult != CUDA_SUCCESS) {
                return 1;
            }

            curesult = cuCtxCreate(&cuContext, 0, cuDevice);
            if (curesult != CUDA_SUCCESS) {
                return 1;
            }
            break;
#endif
#ifdef _ENABLE_OPENACC_
        case openacc:
            if ((str = getenv("LOCAL_RANK")) != NULL) {
                dev_count = acc_get_num_devices(acc_device_not_host);
                local_rank = atoi(str);
                dev_id = local_rank % dev_count;
            }

            acc_set_device_num (dev_id, acc_device_not_host);
            break;
#endif
        default:
            fprintf(stderr, "Invalid device type, should be cuda or openacc\n");
            return 1;
    }

    return 0;
}

int
cleanup_accel (void)
{
#ifdef _ENABLE_CUDA_
     CUresult curesult = CUDA_SUCCESS;
#endif

     switch (options.accel) {
#ifdef _ENABLE_CUDA_
        case cuda:
            curesult = cuCtxDestroy(cuContext);

            if (curesult != CUDA_SUCCESS) {
                return 1;
            }
            break;
#endif
#ifdef _ENABLE_OPENACC_
        case openacc:
            acc_shutdown(acc_device_not_host);
            break;
#endif
        default:
            fprintf(stderr, "Invalid accel type, should be cuda or openacc\n");
            return 1;
    }

    return 0;
}


int
allocate_device_buffer (char ** buffer)
{
#ifdef _ENABLE_CUDA_
    cudaError_t cuerr = cudaSuccess;
#endif

    switch (options.accel) {
#ifdef _ENABLE_CUDA_
        case cuda:
            cuerr = cudaMalloc((void **)buffer, MAX_MSG_SIZE);

            if (cudaSuccess != cuerr) {
                fprintf(stderr, "Could not allocate device memory\n");
                return 1;
            }
            break;
#endif
#ifdef _ENABLE_OPENACC_
        case openacc:
            *buffer = acc_malloc(MAX_MSG_SIZE);
            if (NULL == *buffer) {
                fprintf(stderr, "Could not allocate device memory\n");
                return 1;
            }
            break;
#endif
        default:
            fprintf(stderr, "Could not allocate device memory\n");
            return 1;
    }

    return 0;
}

int
free_device_buffer (void * buf)
{
    switch (options.accel) {
#ifdef _ENABLE_CUDA_
        case cuda:
            cudaFree(buf);
            break;
#endif
#ifdef _ENABLE_OPENACC_
        case openacc:
            acc_free(buf);
            break;
#endif
        default:
            /* unknown device */
            return 1;
    }

    return 0;
}

void
set_device_memory (void * ptr, int data, size_t size)
{
#ifdef _ENABLE_OPENACC_
    size_t i;
    char * p = (char *)ptr;
#endif

    switch (options.accel) {
#ifdef _ENABLE_CUDA_
        case cuda:
            cudaMemset(ptr, data, size);
            break;
#endif
#ifdef _ENABLE_OPENACC_
        case openacc:
#pragma acc parallel loop deviceptr(p)
            for(i = 0; i < size; i++) {
                p[i] = data;
            }
            break;
#endif
        default:
            break;
    }
}

int
allocate_memory (char ** sbuf, char ** rbuf)
{
    unsigned long align_size = sysconf(_SC_PAGESIZE);

    switch (options.accel) {
        case cuda:
        case openacc:       
            if (allocate_device_buffer(sbuf)) { 
                 return 0;
            }
            if (allocate_device_buffer(rbuf)) { 
                 return 0;
            }
            break;
        default:
            if (posix_memalign((void **)sbuf, align_size, MAX_MSG_SIZE)) { 
                 return 0;
            }
            if (posix_memalign((void **)rbuf, align_size, MAX_MSG_SIZE)) {
                 return 0;
            }
    }   

    return 0; 
}

void
touch_data (void * sbuf, void * rbuf, int rank, size_t size)
{
    if ((0 == rank && 'H' == options.src) ||
            (1 == rank && 'H' == options.dst)) {
        memset(sbuf, 'a', size);
        memset(rbuf, 'b', size);
    } else {
        set_device_memory(sbuf, 'a', size);
        set_device_memory(rbuf, 'b', size);
    }
}

void
free_memory (void * sbuf, void * rbuf)
{
    switch (options.accel) {
        case cuda:
        case openacc:
            free_device_buffer(sbuf);
            free_device_buffer(rbuf);
            break;
        default:
            free(sbuf);
            free(rbuf);
    }
}
/* vi: set sw=4 sts=4 tw=80: */
