#pragma once

#include <thread>
#include <vector>
#include <stdlib.h>
#include <fstream>
#include <stdio.h>
#include <unordered_map>
#include <string>
#include <mpi.h>
#include "utils.h"
#include <mutex>

using namespace std;

class Peer {
private:
	int num_tasks;
	int rank;
	mutex mtx;

	unordered_map<string, vector<string>> wanted_files_hashes;

	unordered_map<string, vector<string>> owned_files;
	vector<string> wanted_files;
	int segment_count;

	void download_thread_func();

    void upload_thread_func();
	
	void req_file_swarm_from_tracker(string file_name);
	
	void save_file(string wanted_file_name);

	void send_peer_update_to_tracker(string wanted_file_name);

	void send_download_completed_to_tracker(string file_name);

	void send_all_downloads_completed_to_tracker();
	
	void parse_initial_files();

	void send_initial_files_to_tracker();

	vector<int> recv_file_swarm_from_tracker(string file_name);

	void send_owned_files_to_tracker();
	
	void start_and_join_threads();

public:
	Peer(int numtasks, int rank) : num_tasks(numtasks), rank(rank), segment_count(0) {}

	void init();

};
