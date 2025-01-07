#include "tracker.h"

#include <mpi.h>
#include <thread>
#include <stdio.h>
#include <stdlib.h>
#include <fstream>
#include <vector>
#include <unordered_map>
#include <sstream>
#include <algorithm>

using namespace std;

void Tracker::init() {
	receive_initial_files_from_clients();
	
	acknowledge_initial_files();
	
	start_mediating_the_swarms();

	signal_all_seeds_to_terminate();
}

/* The Tracker assumes the intermediary role of updating each file's swarm
 * and signaling the clients accordingly as long as there is at least one
 * peer remaining, that is, not all of the clients become seeds */
void Tracker::start_mediating_the_swarms() {
	int client_count = num_tasks - 1;
	while (downloads_completed_nr_ < client_count) {
		MPI_Status status;
		int type;
		MPI_Recv(&type, 1, MPI_INT, MPI_ANY_SOURCE, TRACKER_TAG, MPI_COMM_WORLD, &status);

		// check the type of message received from client
		switch(type) {
			case FILE_REQUEST:
				swarm_req(status.MPI_SOURCE);
				break;
			case PEER_UPDATE:
				peer_update(status.MPI_SOURCE);
				break;
			case DOWNLOAD_COMPLETED:
				download_completed(status.MPI_SOURCE);
				break;
			case ALL_DOWNLOADS_COMPLETED:
				all_downloads_completed(status.MPI_SOURCE);
				break;
		}
		cout << "[TRACKER]: Downloads completed: " << downloads_completed_nr_ << endl;
	}
	cout << "[TRACKER]: DONE." << endl;
}

/* Once all clients become seeds, there are no more clients which demand segments
 * so just notify all clients to close their upload thread and terminate */
void Tracker::signal_all_seeds_to_terminate() {
	string terminate = "TERMINATE";
	int size = terminate.size();
	for (int i = 1; i < num_tasks; i++) {
		MPI_Send(&size, 1, MPI_INT, i, UPLOAD_TAG, MPI_COMM_WORLD);
		MPI_Send(terminate.c_str(), terminate.size(), MPI_CHAR, i, UPLOAD_TAG, MPI_COMM_WORLD);
	}
}

void Tracker::acknowledge_initial_files() {
	// when received all lists, send OK to all clients
	int ack = 1;
	for (int i = 1; i < num_tasks; i++) {
		MPI_Send(&ack, 1, MPI_INT, i, TRACKER_TAG, MPI_COMM_WORLD);
	}
}

string Tracker::get_file_swarm(string file_name) {
    string swarm;
	vector<int>& owners = swarms[file_name];

	/* Number of owners in the swarm */
	swarm += to_string(owners.size()) + " ";
	/* File owner's rank */
	for (auto owner : owners) {
		swarm += to_string(owner) + " ";
	}
	
	return swarm;
}

void Tracker::swarm_req(int source) {
	int size;
	// get the name of the requested file
	MPI_Recv(&size, 1, MPI_INT, source, TRACKER_TAG, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
	char buf[size + 1];
	MPI_Recv(buf, size, MPI_CHAR, source, TRACKER_TAG, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
	buf[size] = '\0';
	string file_name(buf);

	string swarm = get_file_swarm(file_name);

	/* Now add the file's hashes so the client can verify correctness */
	swarm += to_string(file_content[file_name].size()) + " ";
	for (auto hash : file_content[file_name]) {
		swarm += hash + " ";
	}

	// send the response to the client
	cout << "[TRACKER]: Sending swarm for file " << file_name << " to " << source << endl;
	size = swarm.size();
	MPI_Send(&size, 1, MPI_INT, source, TRACKER_TAG, MPI_COMM_WORLD);
	MPI_Send(swarm.c_str(), swarm.size(), MPI_CHAR, source, TRACKER_TAG, MPI_COMM_WORLD);
}

void Tracker::peer_update(int client_rank) {
	int buf_size;
	MPI_Recv(&buf_size, 1, MPI_INT, client_rank, TRACKER_TAG, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
	char buf[buf_size + 1];
	MPI_Recv(buf, buf_size, MPI_CHAR, client_rank, TRACKER_TAG, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
	buf[buf_size] = '\0';
	string file_name(buf);
	if (swarms.find(file_name) == swarms.end()) {
		swarms[file_name].push_back(client_rank);
	}
}

void Tracker::receive_initial_files_from_clients() {
    for (int r = 1; r < num_tasks; r++) {
        seed_initial_update(r);
    }
}

void Tracker::seed_initial_update(int source) {
	// get the list of owned files from the client
	int size;
	MPI_Recv(&size, 1, MPI_INT, source, TRACKER_TAG, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
	char buf[size + 1];
	MPI_Recv(buf, size, MPI_CHAR, source, TRACKER_TAG, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
	buf[size] = '\0';
	// parse the list of owned files
	string file_list(buf);
	parse_peer_file_list(file_list, source);
}

void Tracker::download_completed(int client_rank) {
	int size;
	MPI_Recv(&size, 1, MPI_INT, client_rank, TRACKER_TAG, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
	char buf[size + 1];
	MPI_Recv(buf, size, MPI_CHAR, client_rank, TRACKER_TAG, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
	buf[size] = '\0';
	string file_name(buf);
}

void Tracker::all_downloads_completed(int source) {
	// update the number of clients that finished downloading all their files
	cout << "[TRACKER]: Peer " << source << " finished downloading all files." << endl;
	downloads_completed_nr_++;
}

void Tracker::parse_peer_file_list(string file_list, int rank) {
	stringstream ss(file_list);

	string file_name;
	string segment_hash;
	int files_nr;
	int segment_nr;

	// parse the list of owned files
	ss >> files_nr;
	while (ss >> file_name) {
		swarms[file_name].push_back(rank);
		ss >> segment_nr;
		for (int i = 1; i <= segment_nr; i++) {
			ss >> segment_hash;
			file_content[file_name].push_back(segment_hash);
		}
	}
}
