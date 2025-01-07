#pragma once

#include "utils.h"

using namespace std;

class Tracker {
private:
	int num_tasks;
	/* Counter for how many clients finished downloading all their wanted files */
	int clients_done = 0;

	/* Stores each file's hashes; when a client requests a certain file's swarm, 
	 * the tracker will send it along with all that file's hashes, so the client 
	 * knows what hashes it needs to request from peers/seeds. */
	unordered_map<string, vector<string>> file_content;

	/* Stores a list of associated peers/seeds for a certain file */
    unordered_map<string, vector<int>> swarms;

	/* Stores a file's list of peers and seeds */
	unordered_map<string, fcb> file_control_blocks;

	/* Handles a swarm requests from clients */
	void swarm_req(int client_rank);
	
	/* Sends an ACK to the initial clients so they can start their download/upload threads */
	void acknowledge_initial_files();

	/* Tracks and mediates swarms during the downloading phase */
	void start_mediating_the_swarms();

	/* Signals all peers/seeds to terminate after all clients finished their downloading phase */
	void signal_all_seeds_to_terminate();

	/* Handles the initial update from seeds */
	void seed_initial_update(int source);

	/* Retrieves and constructs a file's swarm as a string */
	string get_file_swarm(string file_name);

	/* Updates the peer list when a new client gets a file */
	void peer_update(int client_rank);
	
	/* Receives the initial files from all clients */
	void receive_initial_files_from_clients();

	/* Handles a signal from a client that it finished downloading a file */
	void download_completed(int client_rank);
	
	/* Handles case when a client has finished downloading all files */
	void all_downloads_completed(int client_rank);
	
	/* Parses the initial file list from a seeding client */
	void parse_seed_file_list(string file_list, int rank);

public:
	Tracker(int numtasks) : num_tasks(numtasks) {}

	void init();
};
