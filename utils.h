#pragma once

#include <string>

#define check_mpi_error(n) __check_mpi_error(__FILE__, __LINE__, n)

/* Taken from: https://stackoverflow.com/questions/72949381/openmpi-backtrace-with-line-numbers */
inline void __check_mpi_error(const char *file, const int line, const int n)
{
    char errbuffer[MPI_MAX_ERROR_STRING];
    int errlen;

    if (n != MPI_SUCCESS)
    {
        MPI_Error_string(n, errbuffer, &errlen);
        printf("MPI-error: %s\n", errbuffer);
        printf("Location: %s:%i\n", file, line);
        MPI_Abort(MPI_COMM_WORLD, n);
    }
}


constexpr int TRACKER_RANK = 0;
constexpr int MAX_FILES = 10;
constexpr int MAX_FILENAME = 15;
constexpr int HASH_SIZE = 32;
constexpr int MAX_CHUNKS = 100;

constexpr int OK = 1;

constexpr int FILE_REQUEST = 2;
constexpr int PEER_UPDATE = 3;
constexpr int DOWNLOAD_COMPLETED = 4;
constexpr int ALL_DOWNLOADS_COMPLETED = 5;

constexpr int TRACKER_TAG = 1;
constexpr int DOWNLOAD_TAG = 2;
constexpr int UPLOAD_TAG = 3;
