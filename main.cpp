#include <mpi.h>
#include <thread>
#include <stdio.h>
#include <stdlib.h>
#include <fstream>
#include <vector>
#include <unordered_map>

#include "peer.h"
#include "tracker.h"
#include "utils.h"


int main(int argc, char *argv[]) {
    int numtasks, rank;
 
    int provided;
    MPI_Init_thread(&argc, &argv, MPI_THREAD_MULTIPLE, &provided);
    if (provided < MPI_THREAD_MULTIPLE) {
        fprintf(stderr, "MPI nu are suport pentru multi-threading\n");
        exit(-1);
    }
    MPI_Comm_size(MPI_COMM_WORLD, &numtasks);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);

    if (rank == TRACKER_RANK) {
		auto tracker = Tracker(numtasks, rank);
        tracker.init();
    } else {
		auto peer = Peer(numtasks, rank);
        peer.init();
    }
    MPI_Barrier(MPI_COMM_WORLD);
    MPI_Finalize();
}
