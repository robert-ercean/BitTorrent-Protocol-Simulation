#include "tracker.h"

using namespace std;

/* Encapsulates the Tracker's workflow in a single function for 
 * better readability */
void Tracker::init() {
    receive_initial_files_from_clients();

    acknowledge_initial_files();

    start_mediating_the_swarms();

    signal_all_seeds_to_terminate();
}

void Tracker::start_mediating_the_swarms() {
    int client_count = num_tasks - 1;
    while (clients_done < client_count) {
        MPI_Status status;
		int client_rank;
        int type;
        CHECK_MPI_RET(MPI_Recv(&type, 1, MPI_INT, MPI_ANY_SOURCE, TRACKER_TAG, MPI_COMM_WORLD, &status));
		client_rank = status.MPI_SOURCE;
        switch(type) {
            case SWARM_REQUEST:
                swarm_req(client_rank);
                break;
            case PEER_UPDATE:
                peer_update(client_rank);
                break;
            case SINGLE_FILE_DOWNLOAD_COMPLETED:
                download_completed(client_rank);
                break;
            case CLIENT_GOT_ALL_FILES:
                all_downloads_completed(client_rank);
                break;
        }
        cerr << "[TRACKER]: Clients who've completed all their downloads: " << clients_done << endl;
    }
    cerr << "[TRACKER]: DONE mediating the swarms, exiting" << endl;
}

/* All downloads are complete so signal all clients 
 * to gracefully close their threads and terminate
 * their execution */
void Tracker::signal_all_seeds_to_terminate() {
    string terminate = "TERMINATE";
    int size = terminate.size();
    for (int i = 1; i < num_tasks; i++) {
        CHECK_MPI_RET(MPI_Send(&size, 1, MPI_INT, i, UPLOAD_TAG, MPI_COMM_WORLD));
        CHECK_MPI_RET(MPI_Send(terminate.c_str(), terminate.size(), MPI_CHAR, i, UPLOAD_TAG, MPI_COMM_WORLD));
    }
}

/* Sends the ACK to the inital seeds so they know 
 * they can start their download and upload threads */
void Tracker::acknowledge_initial_files() {
    int ack = 1;
    for (int i = 1; i < num_tasks; i++) {
        CHECK_MPI_RET(MPI_Send(&ack, 1, MPI_INT, i, TRACKER_TAG, MPI_COMM_WORLD));
    }
}

/* Constructs a string containing all peers / seeds of a file */
string Tracker::get_file_swarm(string file_name) {
    string swarm;
    vector<int>& owners = swarms[file_name];

    /* Get all seeds / peers of this file */
    swarm += to_string(owners.size()) + " ";
    for (auto owner : owners) {
        swarm += to_string(owner) + " ";
    }
    return swarm;
}

/* Receives a swarm_request from a client */
void Tracker::swarm_req(int source) {
    int size;
    CHECK_MPI_RET(MPI_Recv(&size, 1, MPI_INT, source, TRACKER_TAG, MPI_COMM_WORLD, MPI_STATUS_IGNORE));
    char buf[size + 1];
    CHECK_MPI_RET(MPI_Recv(buf, size, MPI_CHAR, source, TRACKER_TAG, MPI_COMM_WORLD, MPI_STATUS_IGNORE));
    buf[size] = '\0';
    string file_name(buf);

    string swarm = get_file_swarm(file_name);

    // Now add the file's hashes so the client can verify correctness
    swarm += to_string(file_content[file_name].size()) + " ";
    for (auto hash : file_content[file_name]) {
        swarm += hash + " ";
    }

    // send the response to the client
    cerr << "[TRACKER]: Sending swarm for file " << file_name << " to " << source << endl;
    size = swarm.size();
    CHECK_MPI_RET(MPI_Send(&size, 1, MPI_INT, source, TRACKER_TAG, MPI_COMM_WORLD));
    CHECK_MPI_RET(MPI_Send(swarm.c_str(), swarm.size(), MPI_CHAR, source, TRACKER_TAG, MPI_COMM_WORLD));
}

void Tracker::peer_update(int client_rank) {
    int buf_size;
    CHECK_MPI_RET(MPI_Recv(&buf_size, 1, MPI_INT, client_rank, TRACKER_TAG, MPI_COMM_WORLD, MPI_STATUS_IGNORE));
    char buf[buf_size + 1];
    CHECK_MPI_RET(MPI_Recv(buf, buf_size, MPI_CHAR, client_rank, TRACKER_TAG, MPI_COMM_WORLD, MPI_STATUS_IGNORE));
    buf[buf_size] = '\0';
    string file_name(buf);
    if (swarms.find(file_name) == swarms.end()) {
        swarms[file_name].push_back(client_rank);
    }
	/* Add the client to this file's peer list if isn't present already */
	if (find(file_control_blocks[file_name].peers.begin(), file_control_blocks[file_name].peers.end(), client_rank) == file_control_blocks[file_name].peers.end()) {
		file_control_blocks[file_name].peers.push_back(client_rank);
	}
}

void Tracker::receive_initial_files_from_clients() {
    for (int r = 1; r < num_tasks; r++) {
        seed_initial_update(r);
    }
}

void Tracker::seed_initial_update(int source) {
    int size;
    CHECK_MPI_RET(MPI_Recv(&size, 1, MPI_INT, source, TRACKER_TAG, MPI_COMM_WORLD, MPI_STATUS_IGNORE));
    char buf[size + 1];
    CHECK_MPI_RET(MPI_Recv(buf, size, MPI_CHAR, source, TRACKER_TAG, MPI_COMM_WORLD, MPI_STATUS_IGNORE));
    buf[size] = '\0';
    /* Parse the file content */
    string file_list(buf);
    parse_seed_file_list(file_list, source);
}

/* Handles the signal from a client that it finished downloading 
 * a certain file and marks him as a seed for that file */
void Tracker::download_completed(int client_rank) {
    int size;
    CHECK_MPI_RET(MPI_Recv(&size, 1, MPI_INT, client_rank, TRACKER_TAG, MPI_COMM_WORLD, MPI_STATUS_IGNORE));
    char buf[size + 1];
    CHECK_MPI_RET(MPI_Recv(buf, size, MPI_CHAR, client_rank, TRACKER_TAG, MPI_COMM_WORLD, MPI_STATUS_IGNORE));
    buf[size] = '\0';
    string file_name(buf);
	file_control_blocks[file_name].seeds.push_back(client_rank);
	/* Now remove the client from the file's peer list as it is now a seed */
	file_control_blocks[file_name].peers.erase(
		remove(file_control_blocks[file_name].peers.begin(), file_control_blocks[file_name].peers.end(), client_rank),
		file_control_blocks[file_name].peers.end()
	);
}

void Tracker::all_downloads_completed(int source) {
    // update the number of clients that finished downloading all their files
    cerr << "[TRACKER]: Peer " << source << " finished downloading all files." << endl;
    clients_done++;
}

void Tracker::parse_seed_file_list(string file_list, int rank) {
    stringstream ss(file_list);

    string file_name;
    string segment_hash;
    int files_nr;
    int segment_nr;

    ss >> files_nr;
    while (ss >> file_name) {
        swarms[file_name].push_back(rank);
		file_control_blocks[file_name].seeds.push_back(rank);
        ss >> segment_nr;
        if (!file_content[file_name].empty()) {
            for (int i = 1; i <= segment_nr; i++) {
                ss >> segment_hash;
            }
            continue;    
        }
        for (int i = 1; i <= segment_nr; i++) {
            ss >> segment_hash;
            file_content[file_name].push_back(segment_hash);
        }
    }
}
