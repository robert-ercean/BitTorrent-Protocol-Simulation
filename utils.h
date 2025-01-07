#pragma once

#include <mpi.h>
#include <mutex>
#include <thread>
#include <stdio.h>
#include <stdlib.h>
#include <fstream>
#include <vector>
#include <unistd.h>
#include <unordered_map>
#include <sstream>
#include <algorithm>
#include <iostream>

using namespace std;

/* Template taken from: https://stackoverflow.com/questions/72949381/openmpi-backtrace-with-line-numbers
 *
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

constexpr int ACK = 1;

constexpr int SWARM_REQUEST = 55;
constexpr int PEER_UPDATE = 44;
constexpr int SINGLE_FILE_DOWNLOAD_COMPLETED = 33;
constexpr int CLIENT_GOT_ALL_FILES = 22;

constexpr int TRACKER_TAG = 1;
constexpr int DOWNLOAD_TAG = 2;
constexpr int UPLOAD_TAG = 3;

/* File control block for a file 
 * contains a list of this file's seeds and peers */
typedef struct {
    vector<int> seeds;
    vector<int> peers;
} fcb;