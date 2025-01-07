#pragma once

#include "utils.h"

using namespace std;

class Peer {
private:
	int num_tasks;
	int rank;
	mutex owned_files_mtx;

	unordered_map<string, vector<string>> wanted_files_hashes;
	unordered_map<string, vector<string>> owned_files;

	vector<string> wanted_files;
	long long segment_count;


	/* File handling */
	void save_file(string wanted_file_name);
	void parse_initial_files();

	/* MPI Communication */
	void wait_for_initial_ack();
	void send_peer_update_to_tracker(string file_name);
	void send_download_completed_to_tracker(string file_name);
	void send_all_downloads_completed_to_tracker();
	void send_owned_files_to_tracker();
	void req_file_swarm_from_tracker(string file_name);
	vector<int> recv_file_swarm_from_tracker(string file_name);
	int check_if_segment_is_owned(string file_name, string segment_hash);

	/* Thread-related funcs */
	void start_and_join_threads();
	void download_thread_func();
	void upload_thread_func();

public:
	Peer(int numtasks, int rank) : num_tasks(numtasks), rank(rank), segment_count(0) {}

	void init();
};
