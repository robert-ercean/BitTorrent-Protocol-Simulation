#pragma once

#include <mpi.h>
#include <thread>
#include <stdio.h>
#include <stdlib.h>
#include <fstream>
#include <vector>
#include <unordered_map>

#include "utils.h"

using namespace std;

class Tracker {
private:
	int num_tasks;
	int rank_;
	int downloads_completed_nr_ = 0;

	unordered_map<string, vector<string>> file_content;

    unordered_map<string, vector<int>> swarms;

	void swarm_req(int source);
	
	void acknowledge_initial_files();

	void start_mediating_the_swarms();

	void signal_all_seeds_to_terminate();

	void seed_initial_update(int source);

	string get_file_swarm(string file_name);

	void peer_update(int source);
	
	void receive_initial_files_from_clients();

	void download_completed(int source);
	
	void all_downloads_completed(int source);
	
	void parse_peer_file_list(string file_list, int rank);

public:
	Tracker(int numtasks, int rank) {
		num_tasks = numtasks;
		rank_ = rank;
	}

	void init();
};