#include "peer.h"

#include <mpi.h>
#include <thread>
#include <stdio.h>
#include <stdlib.h>
#include <fstream>
#include <vector>
#include <unordered_map>
#include <sstream>
#include <algorithm>

#include "utils.h"

using namespace std;

void Peer::init() {
	parse_initial_files();

	send_owned_files_to_tracker();

	start_and_join_threads();
}

void Peer::start_and_join_threads() {
	thread download_thread(&Peer::download_thread_func, this);
	thread upload_thread(&Peer::upload_thread_func, this);

	download_thread.join();
	upload_thread.join();
}

void Peer::download_thread_func() {
    // Wait for the initial ACK from the tracker.
    int ack;
    MPI_Recv(&ack, 1, MPI_INT, TRACKER_RANK, TRACKER_TAG, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
    if (ack != 1) {
        cout << "Tracker did not send OK message." << endl;
        exit(-1);
    }
	cout << "Got the ACK: " << rank << endl;
    // For each file that this peer wants:
    for (auto &wanted_file_name : wanted_files) {
        // 1) Get the swarm from the tracker
		cout << "[Peer " << rank << "]: Requesting swarm for file " << wanted_file_name << endl;
        req_file_swarm_from_tracker(wanted_file_name);
        vector<int> file_owners = recv_file_swarm_from_tracker(wanted_file_name);
		cout << "[Peer " << rank << "]: Got swarm for file " << wanted_file_name << " received." << endl;
		// print the swarm
		cout << "Swarm for file " << wanted_file_name << " is: ";
		for (auto owner : file_owners) {
			cout << owner << " ";
		}
		cout << endl;
        if (file_owners.empty()) {
            cerr << "[Peer " << rank << "]: No swarm found for file "
                 << wanted_file_name << ". Cannot download.\n";
            continue;
        }

        // 2) Gather all segment hashes we need (assuming you have them somewhere)
        vector<string> hashes_to_acquire = wanted_files_hashes[wanted_file_name];
        int total_segments_for_file = (int)hashes_to_acquire.size();

        // 3) Download each segment
        for (int seg_idx = 0; seg_idx < total_segments_for_file; seg_idx++) {
            const string &segment_hash = hashes_to_acquire[seg_idx];

            // Round-robin among file_owners
            bool segment_downloaded = false;
            int attempts = 0;

            while (!segment_downloaded && attempts < (int)file_owners.size()) {
                int peer_index = (seg_idx + attempts) % file_owners.size();
                int target_peer = file_owners[peer_index];
				if (target_peer == rank) {
					attempts++;
					continue;
				}
				string buf = wanted_file_name + " " + segment_hash;
				// send the size
				int size = buf.size();
				MPI_Send(&size, 1, MPI_INT, target_peer, UPLOAD_TAG, MPI_COMM_WORLD);
				// debug that im requesting from the right peer
				cout << "[Peer " << rank << "]: Requesting segment " << segment_hash << " from peer " << target_peer << endl;
                // Request the segment
                MPI_Send(buf.c_str(), buf.size(), MPI_CHAR, 
                         target_peer, UPLOAD_TAG, MPI_COMM_WORLD);

                // Wait for the response (ACK == OK, else considered NACK)
                int response;
                MPI_Recv(&response, 1, MPI_INT, target_peer, DOWNLOAD_TAG, 
                         MPI_COMM_WORLD, MPI_STATUS_IGNORE);

                if (response == OK) {
                    // We have successfully downloaded the segment
					cout << "[Peer " << rank << "]: Successfully downloaded segment " << segment_hash << " from peer" << target_peer << endl;
                    mtx.lock();
					if (find(owned_files[wanted_file_name].begin(), owned_files[wanted_file_name].end(), segment_hash) == owned_files[wanted_file_name].end())
						owned_files[wanted_file_name].push_back(segment_hash);
                    mtx.unlock();
                    segment_count++;
                    segment_downloaded = true;

                    // After every 10 new segments, notify tracker and re-fetch the swarm
                    if (segment_count % 10 == 0) {
                        send_peer_update_to_tracker(wanted_file_name);

                        // Re-request the swarm to check if new complete owners joined
                        req_file_swarm_from_tracker(wanted_file_name);
                        file_owners = recv_file_swarm_from_tracker(wanted_file_name);
                    }
                } // else NACK => try next peer in the round-robin
				else {
					cout << "DENIED" << endl;
				}
                attempts++;
            }

            if (!segment_downloaded) {
                cerr << "[Peer " << rank << "] Failed to download segment "
                     << segment_hash << " of file " << wanted_file_name << endl;
            }
        }

        // 4) Notify tracker: we now have the entire file
        send_download_completed_to_tracker(wanted_file_name);
        save_file(wanted_file_name);
    }

    // 5) Finally, notify tracker that we have finished all our downloads
	cout << "[Peer " << rank << "]: Finished all downloads." << endl;
    send_all_downloads_completed_to_tracker();
}

void Peer::send_all_downloads_completed_to_tracker() {
	int action = ALL_DOWNLOADS_COMPLETED;
	MPI_Send(&action, 1, MPI_INT, TRACKER_RANK, TRACKER_TAG, MPI_COMM_WORLD);
}

void Peer::send_peer_update_to_tracker(string wanted_file_name) {
	string buffer = wanted_file_name;
	int size = buffer.size();
	int action = PEER_UPDATE;
	MPI_Send(&action, 1, MPI_INT, TRACKER_RANK, TRACKER_TAG, MPI_COMM_WORLD);
	MPI_Send(&size, 1, MPI_INT, TRACKER_RANK, TRACKER_TAG, MPI_COMM_WORLD);
	MPI_Send(buffer.c_str(), buffer.size(), MPI_CHAR, TRACKER_RANK, TRACKER_TAG, MPI_COMM_WORLD);
}

void Peer::send_download_completed_to_tracker(string file_name) {
	string buf = file_name;
	int action = DOWNLOAD_COMPLETED;
	MPI_Send(&action, 1, MPI_INT, TRACKER_RANK, TRACKER_TAG, MPI_COMM_WORLD);
	int size = buf.size();
	MPI_Send(&size, 1, MPI_INT, TRACKER_RANK, TRACKER_TAG, MPI_COMM_WORLD);
	MPI_Send(buf.c_str(), buf.size(), MPI_CHAR, TRACKER_RANK, TRACKER_TAG, MPI_COMM_WORLD);
}

void Peer::upload_thread_func() {
	while (1) {
		// wait for request
		int size;
		MPI_Status status;
		MPI_Recv(&size, 1, MPI_INT, MPI_ANY_SOURCE, UPLOAD_TAG, MPI_COMM_WORLD, &status);
		int source = status.MPI_SOURCE;
		char buf[size + 1];
		string file_name;
		string segment_hash;
		MPI_Recv(buf, size, MPI_CHAR, source, UPLOAD_TAG, MPI_COMM_WORLD, &status);
		buf[size] = '\0';
		// debug that im requestion from the right peer
		cout << "[Peer " << rank << "]: Received request for segment " << buf << " from peer " << source << endl;
		stringstream ss(buf);
		ss >> file_name >> segment_hash;

		if (file_name == "TERMINATE") {
			break;
		}
		mtx.lock();
		/* Check if we have the wanted segment, if yes send ACK, if not send NACK */
		if (find(owned_files[file_name].begin(), owned_files[file_name].end(), segment_hash) != owned_files[file_name].end()) {
			int response = OK;
			cout << "[Peer " << rank << "]: Sending segment " << segment_hash << " to peer " << source << endl;
			MPI_Send(&response, 1, MPI_INT, source, DOWNLOAD_TAG, MPI_COMM_WORLD);
		} else {
			int response = !OK;
			MPI_Send(&response, 1, MPI_INT, source, DOWNLOAD_TAG, MPI_COMM_WORLD);
		}
		mtx.unlock();
	}
	cout << "[Peer " << rank << "]: Terminating upload thread." << endl;
}

void Peer::parse_initial_files() {
	ifstream fin("in" + to_string(rank) + ".txt");
	int segment_nr;
	string file_name;
	string segment_hash;
	int file_count;
	// read owned files
	fin >> file_count;
	for (int i = 0; i < file_count; i++) {
		fin >> file_name >> segment_nr;
		for (int j = 1; j <= segment_nr; j++) {
			fin >> segment_hash;
			owned_files[file_name].push_back(segment_hash);
		}
	}

	// read wanted files
	fin >> file_count;
	for (int i = 0; i < file_count; i++) {
		fin >> file_name;
		wanted_files.push_back(file_name);
	}
	fin.close();
}

vector<int> Peer::recv_file_swarm_from_tracker(string file_name) {
	int buf_size;
	MPI_Recv(&buf_size, 1, MPI_INT, TRACKER_RANK, TRACKER_TAG, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

	char response_buf[buf_size + 1];
	MPI_Recv(response_buf, buf_size, MPI_CHAR, TRACKER_RANK, TRACKER_TAG, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
	
	response_buf[buf_size] = '\0';
	// return the list of seeds/peers
	vector<int> file_owners;
	stringstream file_owners_str(response_buf);
	int owners_count;
	int rank;
	file_owners_str >> owners_count;
	for (int i = 0; i < owners_count; i++) {
		file_owners_str >> rank;
		file_owners.emplace_back(rank);
	}
	if (wanted_files_hashes[file_name].empty()) {
		int segment_count;
		file_owners_str >> segment_count;
		for (int i = 0; i < segment_count; i++) {
			string segment_hash;
			file_owners_str >> segment_hash;
			wanted_files_hashes[file_name].push_back(segment_hash);
		}
	}
	return file_owners;
}

void Peer::req_file_swarm_from_tracker(string file_name) {
	int file_name_size = file_name.size();

	// Request the file's swarm from the tracker
	MPI_Send(&FILE_REQUEST, 1, MPI_INT, TRACKER_RANK, TRACKER_TAG, MPI_COMM_WORLD);
	MPI_Send(&file_name_size, 1, MPI_INT, TRACKER_RANK, TRACKER_TAG, MPI_COMM_WORLD);
	MPI_Send(file_name.c_str(), file_name.size(), MPI_CHAR, TRACKER_RANK, TRACKER_TAG, MPI_COMM_WORLD);
}

/* Sends the initial file hashes to the tracker 
 * The client will act as a SEED for these files */
void Peer::send_owned_files_to_tracker() {
	string buffer = to_string(owned_files.size()) + " ";
	for (auto &[file_name, hashes] : owned_files) {
		buffer += file_name + " " + to_string(hashes.size()) + " ";
		for (auto hash : hashes) {
			buffer += hash + " ";
		}
	}

	int size = buffer.size();
	// send the list of owned files to the tracker
	MPI_Send(&size, 1, MPI_INT, TRACKER_RANK, TRACKER_TAG, MPI_COMM_WORLD);
	MPI_Send(buffer.c_str(), buffer.size(), MPI_CHAR, TRACKER_RANK, TRACKER_TAG, MPI_COMM_WORLD);
}

void Peer::save_file(string wanted_file_name) {
	ofstream fout("client" + to_string(rank)+ "_" + wanted_file_name, ios::app);
	// print no new lines at the end of the file
	for (int i = 0; i < (int)owned_files[wanted_file_name].size(); i++) {
		fout << owned_files[wanted_file_name][i];
		if (i != (int)owned_files[wanted_file_name].size() - 1) {
			fout << "\n";
		}
	}
	fout.close();
}
