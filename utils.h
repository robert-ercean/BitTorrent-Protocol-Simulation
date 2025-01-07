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


enum Constants {
    TRACKER_RANK = 0,
    ACK = 1,
    SWARM_REQUEST = 55,
    PEER_UPDATE = 44,
    SINGLE_FILE_DOWNLOAD_COMPLETED = 33,
    CLIENT_GOT_ALL_FILES = 22,
    TRACKER_TAG = 1,
    DOWNLOAD_TAG = 2,
    UPLOAD_TAG = 3
};


/* File control block for a file 
 * contains a list of this file's seeds and peers */
typedef struct {
    vector<int> seeds;
    vector<int> peers;
} fcb;