#pragma once

#include "utils.h"

using namespace std;

class Tracker {
private:
	int num_tasks;
	int clients_done = 0;

	/* Stores each file's hashes, once a client requests a certain file's swarm 
	 * the tracker will send it, and, additionally will send all that file's hashes
	 * so the client knows what hashes it needs to request from the peers/seeds */
	unordered_map<string, vector<string>> file_content;
	/* Stores a list of associated peers / seeds for a certain file */
    unordered_map<string, vector<int>> swarms;
	/* Stores a file's list of peers and seeds */
	unordered_map<string, fcb> file_control_blocks;

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
	
	void parse_seed_file_list(string file_list, int rank);

public:
	Tracker(int numtasks) {
		num_tasks = numtasks;
	}

	void init();
};