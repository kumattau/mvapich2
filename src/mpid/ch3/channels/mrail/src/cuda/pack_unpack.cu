/* Copyright (c) 2001-2012, The Ohio State University. All rights
 * reserved.
 *
 * This file is part of the MVAPICH2 software package developed by the
 * team members of The Ohio State University's Network-Based Computing
 * Laboratory (NBCL), headed by Professor Dhabaleswar K. (DK) Panda.
 *
 * For detailed copyright and licensing information, please refer to the
 * copyright file COPYRIGHT in the top level MVAPICH2 directory.
 *
 */

__global__ void pack_subarray_double( double *dst, double *src, int nx, int ny, int nz, int sub_nx, int sub_ny, int sub_nz, int h_x, int h_y, int h_z)
{
//==============================================================================
// 2 Registers | 3 arguments
//==============================================================================
    int   i, j, k;
//==============================================================================

    // Identify current thread
    // Notice the +1 shift that is used in order to avoid ghost nodes
    i = blockIdx.x * blockDim.x + threadIdx.x;
    j = blockIdx.y * blockDim.y + threadIdx.y;
    k = blockIdx.z * blockDim.z + threadIdx.z;

    if ( (i < sub_nx) && (j < sub_ny) && (k < sub_nz) ) {
            dst[ i + sub_nx * j + sub_nx * sub_ny * k ] = src[ (i + h_x) + nx * (j + h_y)  + nx * ny * (k + h_z) ];
    }

}

__global__ void unpack_subarray_double( double *dst, double *src, int nx, int ny, int nz, int sub_nx, int sub_ny, int sub_nz, int h_x, int h_y, int h_z)
{
//==============================================================================
// 2 Registers | 3 arguments
//==============================================================================
    int   i, j, k;
//==============================================================================

    // Identify current thread
    // Notice the +1 shift that is used in order to avoid ghost nodes
    i = blockIdx.x * blockDim.x + threadIdx.x;
    j = blockIdx.y * blockDim.y + threadIdx.y;
    k = blockIdx.z * blockDim.z + threadIdx.z;

    if ( (i < sub_nx) && (j < sub_ny) && (k < sub_nz) ) {
            dst[ (i + h_x) + nx * (j + h_y)  + nx * ny * (k + h_z) ] = src[ i + sub_nx * j + sub_nx * sub_ny * k ];
    }

}

__global__ void pack_subarray_float( float *dst, float *src, int nx, int ny, int nz, int sub_nx, int sub_ny, int sub_nz, int h_x, int h_y, int h_z)
{
//==============================================================================
// 2 Registers | 3 arguments
//==============================================================================
    int   i, j, k;
//==============================================================================

    // Identify current thread
    // Notice the +1 shift that is used in order to avoid ghost nodes
    i = blockIdx.x * blockDim.x + threadIdx.x;
    j = blockIdx.y * blockDim.y + threadIdx.y;
    k = blockIdx.z * blockDim.z + threadIdx.z;

    if ( (i < sub_nx) && (j < sub_ny) && (k < sub_nz) ) {
            dst[ i + sub_nx * j + sub_nx * sub_ny * k ] = src[ (i + h_x) + nx * (j + h_y)  + nx * ny * (k + h_z) ];
    }

}

__global__ void unpack_subarray_float( float *dst, float *src, int nx, int ny, int nz, int sub_nx, int sub_ny, int sub_nz, int h_x, int h_y, int h_z)
{
//==============================================================================
// 2 Registers | 3 arguments
//==============================================================================
    int   i, j, k;
//==============================================================================

    // Identify current thread
    // Notice the +1 shift that is used in order to avoid ghost nodes
    i = blockIdx.x * blockDim.x + threadIdx.x;
    j = blockIdx.y * blockDim.y + threadIdx.y;
    k = blockIdx.z * blockDim.z + threadIdx.z;

    if ( (i < sub_nx) && (j < sub_ny) && (k < sub_nz) ) {
            dst[ (i + h_x) + nx * (j + h_y)  + nx * ny * (k + h_z) ] = src[ i + sub_nx * j + sub_nx * sub_ny * k ];
    }

}

__global__ void pack_subarray_char( char *dst, char *src, int nx, int ny, int nz, int sub_nx, int sub_ny, int sub_nz, int h_x, int h_y, int h_z)
{
//==============================================================================
// 2 Registers | 3 arguments
//==============================================================================
    int   i, j, k;
//==============================================================================

    // Identify current thread
    // Notice the +1 shift that is used in order to avoid ghost nodes
    i = blockIdx.x * blockDim.x + threadIdx.x;
    j = blockIdx.y * blockDim.y + threadIdx.y;
    k = blockIdx.z * blockDim.z + threadIdx.z;

    if ( (i < sub_nx) && (j < sub_ny) && (k < sub_nz) ) {
            dst[ i + sub_nx * j + sub_nx * sub_ny * k ] = src[ (i + h_x) + nx * (j + h_y)  + nx * ny * (k + h_z) ];
    }

}

__global__ void unpack_subarray_char( char *dst, char *src, int nx, int ny, int nz, int sub_nx, int sub_ny, int sub_nz, int h_x, int h_y, int h_z)
{
//==============================================================================
// 2 Registers | 3 arguments
//==============================================================================
    int   i, j, k;
//==============================================================================

    // Identify current thread
    // Notice the +1 shift that is used in order to avoid ghost nodes
    i = blockIdx.x * blockDim.x + threadIdx.x;
    j = blockIdx.y * blockDim.y + threadIdx.y;
    k = blockIdx.z * blockDim.z + threadIdx.z;

    if ( (i < sub_nx) && (j < sub_ny) && (k < sub_nz) ) {
            dst[ (i + h_x) + nx * (j + h_y)  + nx * ny * (k + h_z) ] = src[ i + sub_nx * j + sub_nx * sub_ny * k ];
    }

}


extern "C" void pack_subarray( void *dst, void *src, int nx, int ny, int nz, int sub_nx, int sub_ny, int sub_nz, int h_x, int h_y, int h_z, int el_size ) 
{
    int BLOCK_SIZE_X = 16;
    int BLOCK_SIZE_Y = 8;
    int BLOCK_SIZE_Z = 8;

    int GRID_SIZE_X = (sub_nx + BLOCK_SIZE_X - 1) / BLOCK_SIZE_X;
    int GRID_SIZE_Y = (sub_ny + BLOCK_SIZE_Y - 1) / BLOCK_SIZE_Y;
    int GRID_SIZE_Z = (sub_nz + BLOCK_SIZE_Z - 1) / BLOCK_SIZE_Z;

    dim3 dimblock( BLOCK_SIZE_X, BLOCK_SIZE_Y, BLOCK_SIZE_Z );
    dim3 dimgrid( GRID_SIZE_X, GRID_SIZE_Y, GRID_SIZE_Z );

    if (el_size == 4) {
        pack_subarray_float<<<dimgrid, dimblock>>>((float *) dst, (float *) src, nx, ny, nz, sub_nx, sub_ny, sub_nz, h_x, h_y, h_z );
    } else if (el_size == 1) {
        pack_subarray_char<<<dimgrid, dimblock>>>((char *) dst, (char *) src, nx, ny, nz, sub_nx, sub_ny, sub_nz, h_x, h_y, h_z );
    } else if (el_size == 8) {
        pack_subarray_double<<<dimgrid, dimblock>>>((double *) dst, (double *) src, nx, ny, nz, sub_nx, sub_ny, sub_nz, h_x, h_y, h_z );
    }
}

extern "C" void unpack_subarray( void *dst, void *src, int nx, int ny, int nz, int sub_nx, int sub_ny, int sub_nz, int h_x, int h_y, int h_z, int el_size)
{
    int BLOCK_SIZE_X = 16;
    int BLOCK_SIZE_Y = 8;
    int BLOCK_SIZE_Z = 8;

    int GRID_SIZE_X = (sub_nx + BLOCK_SIZE_X - 1) / BLOCK_SIZE_X;
    int GRID_SIZE_Y = (sub_ny + BLOCK_SIZE_Y - 1) / BLOCK_SIZE_Y;
    int GRID_SIZE_Z = (sub_nz + BLOCK_SIZE_Z - 1) / BLOCK_SIZE_Z;

    dim3 dimblock( BLOCK_SIZE_X, BLOCK_SIZE_Y, BLOCK_SIZE_Z );
    dim3 dimgrid( GRID_SIZE_X, GRID_SIZE_Y, GRID_SIZE_Z );

    if (el_size == 4) {
        unpack_subarray_float<<<dimgrid, dimblock>>>((float *) dst, (float *) src, nx, ny, nz, sub_nx, sub_ny, sub_nz, h_x, h_y, h_z );
    } else if (el_size == 1) {
        unpack_subarray_char<<<dimgrid, dimblock>>>((char *) dst, (char *) src, nx, ny, nz, sub_nx, sub_ny, sub_nz, h_x, h_y, h_z );
    } else if (el_size == 8) {
        unpack_subarray_double<<<dimgrid, dimblock>>>((double *) dst, (double *) src, nx, ny, nz, sub_nx, sub_ny, sub_nz, h_x, h_y, h_z );
    }
    
}

__global__ void pack_unpack_vector_double( double *dst, int dpitch, double *src, int spitch, int width, int height)
{
//==============================================================================
// 2 Registers | 2 arguments
//==============================================================================
    int   i, j;
//==============================================================================
    i = blockIdx.x * blockDim.x + threadIdx.x;

    if (i < height) {
        for (j = 0; j < width; j++) {
            dst[i * dpitch + j] = src[i * spitch + j];
        }
    }
}

__global__ void pack_unpack_vector_float( float *dst, int dpitch, float *src, int spitch, int width, int height)
{
//==============================================================================
// 2 Registers | 2 arguments
//==============================================================================
    int   i, j;
//==============================================================================
    i = blockIdx.x * blockDim.x + threadIdx.x;

    if (i < height) {
        for (j = 0; j < width; j++) {
            dst[i * dpitch + j] = src[i * spitch + j];
        }
    }
}

__global__ void pack_unpack_vector_char(  char *dst, int dpitch, char *src, int spitch, int width, int height)
{
//==============================================================================
// 2 Registers | 2 arguments
//==============================================================================
    int   i, j;
//==============================================================================
    i = blockIdx.x * blockDim.x + threadIdx.x;

    if (i < height) {
        for (j = 0; j < width; j++) {
            dst[i * dpitch + j] = src[i * spitch + j];
        }
    }
}

extern "C" void pack_unpack_vector_kernel( void *dst, int dpitch, void *src, int spitch, int width, int height)
{
    int BLOCK_SIZE_X = 256;
    int GRID_SIZE_X = (height + BLOCK_SIZE_X - 1) / BLOCK_SIZE_X;

    dim3 dimblock( BLOCK_SIZE_X, 1, 1);
    dim3 dimgrid( GRID_SIZE_X, 1, 1);

    if ((0 == (width % sizeof(double))) && (0 == (dpitch % sizeof(double))) && (0 == (spitch % sizeof(double)))) {
        pack_unpack_vector_double<<<dimgrid, dimblock>>>((double *) dst, dpitch / sizeof(double), 
					(double *) src, spitch / sizeof(double), width / sizeof(double), height);
    } else if ((0 == (width % sizeof(float))) && (0 == (dpitch % sizeof(float))) && (0 == (spitch % sizeof(float)))) {
        pack_unpack_vector_float<<<dimgrid, dimblock>>>((float *) dst, dpitch / sizeof(float), 
					(float *) src, spitch / sizeof(float), width / sizeof(float), height);
    } else if ((0 == (width % sizeof(char))) && (0 == (dpitch % sizeof(char))) && (0 == (spitch % sizeof(char)))) {
        pack_unpack_vector_char<<<dimgrid, dimblock>>>((char *) dst, dpitch / sizeof(char), 
					(char *) src, spitch / sizeof(char), width / sizeof(char), height);
    }
}

