#pragma once

#include <string>
#include <iostream>
#include <mpi.h>

using namespace std;

// #define check_mpi_error(n) __check_mpi_error(__FILE__, __LINE__, n)

// /* Taken from: https://stackoverflow.com/questions/72949381/openmpi-backtrace-with-line-numbers */
/**
 * Prints MPI errors.
 */
static void check_mpi_error(const char* call, int ret, const char* file, int line) {
    if (ret != MPI_SUCCESS) {
        cerr << "[PEER] MPI call \"" << call << "\" failed at "
             << file << ":" << line 
             << " with error code " << ret << endl;
        // exit(-1);
    }
}

/**
 * Wraps an MPI call and checks its return code.
 */
#define CHECK_MPI_RET(fncall)                 \
    do {                                      \
        int ret = (fncall);                  \
        check_mpi_error(#fncall, ret,        \
                        __FILE__, __LINE__); \
    } while (0)


constexpr int TRACKER_RANK = 0;

constexpr int OK = 1;

constexpr int FILE_REQUEST = 2;
constexpr int PEER_UPDATE = 3;
constexpr int DOWNLOAD_COMPLETED = 4;
constexpr int ALL_DOWNLOADS_COMPLETED = 5;

constexpr int TRACKER_TAG = 1;
constexpr int DOWNLOAD_TAG = 2;
constexpr int UPLOAD_TAG = 3;
