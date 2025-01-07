#include "peer.h"

using namespace std;

/* Encapsulates the Client's workflow in a single function for 
 * better readability */
void Peer::init() {
    parse_initial_files();

    send_owned_files_to_tracker();
	
	wait_for_initial_ack();
    
	start_and_join_threads();
}

/* Wait for the tracker's ACK until we start the download / upload threads */
void Peer::wait_for_initial_ack() {
    int ack;
    CHECK_MPI_RET(MPI_Recv(&ack, 1, MPI_INT, TRACKER_RANK, TRACKER_TAG, MPI_COMM_WORLD, MPI_STATUS_IGNORE));
    if (ack != 1) {
        cerr << "Tracker did not send OK message." << endl;
        exit(-1);
    }
    cerr << "Got the ACK: " << rank << endl;
}

void Peer::start_and_join_threads() {
    thread download_thread(&Peer::download_thread_func, this);
    thread upload_thread(&Peer::upload_thread_func, this);

    download_thread.join();
    upload_thread.join();
}

void Peer::download_thread_func() {
    /* Loop through all the wanted files */
    for (auto &wanted_file_name : wanted_files) {
        /* Get this file's swarm for the tracker */
        cerr << "[Peer " << rank << "]: Requesting swarm for file " << wanted_file_name << endl;
        req_file_swarm_from_tracker(wanted_file_name);
        vector<int> file_owners = recv_file_swarm_from_tracker(wanted_file_name);
        cerr << "[Peer " << rank << "]: Got swarm for file " << wanted_file_name << " received." << endl;
        cerr << "Swarm for file " << wanted_file_name << " is: ";
        for (auto owner : file_owners) {
            cerr << owner << " ";
        }
        cerr << endl;

        if (file_owners.empty()) {
            cerr << "[Peer " << rank << "]: No swarm found for file "
                 << wanted_file_name << ". Cannot download.\n";
            continue;
        }

        /* Get the list of all the needed segments */
        vector<string> hashes_to_acquire = wanted_files_hashes[wanted_file_name];
        int total_segments_for_file = (int)hashes_to_acquire.size();

        /* Download each segment of a file in a round-robin manner 
		 * so no single seed / peer of this file will get too busy */
        for (int seg_idx = 0; seg_idx < total_segments_for_file; seg_idx++) {
            string &segment_hash = hashes_to_acquire[seg_idx];

            bool segment_downloaded = false;
			/* The attempt count */
            int iteration = 0;

            while (!segment_downloaded && iteration < (int)file_owners.size()) {
                /* Decide the Round-Robin index */
				int peer_index = (seg_idx + iteration) % file_owners.size();
                int target_peer = file_owners[peer_index];
                /* We can't download from ourselves a wanted segment as we know
				 * for sure we don't have it and it also doesn't make sense */
				if (target_peer == rank) {
                    iteration++;
                    continue;
                }

                string buf = wanted_file_name + " " + segment_hash;
                int size = (int)buf.size();

                /* Send the segment's hash size */
                CHECK_MPI_RET(MPI_Send(&size, 1, MPI_INT, target_peer, UPLOAD_TAG, MPI_COMM_WORLD));
                cerr << "[Peer " << rank << "]: Requesting segment " 
                     << segment_hash << " from peer " << target_peer << endl;

                /* Send the buffer containing the wanted segment's hash */
                CHECK_MPI_RET(MPI_Send(buf.c_str(), buf.size(), MPI_CHAR, target_peer, UPLOAD_TAG, MPI_COMM_WORLD));

                /* Wait the peer's / seed's response */
                int response;
                CHECK_MPI_RET(
                    MPI_Recv(&response, 1, MPI_INT, target_peer, DOWNLOAD_TAG,MPI_COMM_WORLD, MPI_STATUS_IGNORE));

                if (response == ACK) {
                    /* Only add the this segment to the owned list if a peer / seeds
					 * sent us an ACK */
                    cerr << "[Peer " << rank << "]: Successfully downloaded segment "
                         << segment_hash << " from peer " << target_peer << endl;
					/* Update the owned hashes of the files in a mutually exclusive way for 
					 * synchronizing with the upload thread */
					owned_files_mtx.lock();
					owned_files[wanted_file_name].push_back(segment_hash);
					owned_files_mtx.unlock();
                    segment_count++;
                    segment_downloaded = true;

                    /* After downloading 10 segments, request the swarm again from the tracker
					 * as new seeds / peers may have entered this file's swarm */
                    if (segment_count % 10 == 0) {
						/* Notify the tracker we can also act as a peer for this file */
                        send_peer_update_to_tracker(wanted_file_name);
						/* Send the new swarm request */
                        req_file_swarm_from_tracker(wanted_file_name);
						/* Update the file's swarm */
                        file_owners = recv_file_swarm_from_tracker(wanted_file_name);
                    }
                } else if (response == !ACK){
					/* If we got a NACK, we'll loop again until we get an ACK
					 * from a seed / peer for this segment, until the segment_downloaded
					 * flag gets set to true */
                    cerr << "DENIED" << endl;
                }
                iteration++;
            }

            if (!segment_downloaded) {
                cerr << "[Peer " << rank << "] Failed to download segment "
                     << segment_hash << " of file " << wanted_file_name << endl;
            }
        }

        /* Notifiy the tracker that this client finished downloading a whole file 
		 * so the tracker can mark it as a seed for this file */
        send_download_completed_to_tracker(wanted_file_name);
		/* Write the file's segments to disk */
        save_file(wanted_file_name);
    }

    /* After there are no more files to download, notify the tracker that this client finished */
    cerr << "[Peer " << rank << "]: Finished all downloads." << endl;
    send_all_downloads_completed_to_tracker();
}

/* Signals the tracker that this client has finished downloading
 * ALL of its wanted files */
void Peer::send_all_downloads_completed_to_tracker() {
    int action = CLIENT_GOT_ALL_FILES;
    CHECK_MPI_RET(MPI_Send(&action, 1, MPI_INT, TRACKER_RANK, TRACKER_TAG, MPI_COMM_WORLD));
}

/* Notifies the tracker that this client can act as a peer for a certain file */
void Peer::send_peer_update_to_tracker(string file_name) {
    string buffer = file_name;
    int size = (int)buffer.size();
    int action = PEER_UPDATE;
    CHECK_MPI_RET(MPI_Send(&action, 1, MPI_INT, TRACKER_RANK, TRACKER_TAG, MPI_COMM_WORLD));
    CHECK_MPI_RET(MPI_Send(&size, 1, MPI_INT, TRACKER_RANK, TRACKER_TAG, MPI_COMM_WORLD));
    CHECK_MPI_RET(MPI_Send(buffer.c_str(), buffer.size(), MPI_CHAR, TRACKER_RANK, TRACKER_TAG, MPI_COMM_WORLD));
}

/* Signals the tracker that this client has finished downloading
 * a single wanted file */
void Peer::send_download_completed_to_tracker(string file_name) {
    string buf = file_name;
    int action = SINGLE_FILE_DOWNLOAD_COMPLETED;
    CHECK_MPI_RET(MPI_Send(&action, 1, MPI_INT, TRACKER_RANK, TRACKER_TAG, MPI_COMM_WORLD));
    int size = (int)buf.size();
    CHECK_MPI_RET(MPI_Send(&size, 1, MPI_INT, TRACKER_RANK, TRACKER_TAG, MPI_COMM_WORLD));
    CHECK_MPI_RET(MPI_Send(buf.c_str(), buf.size(), MPI_CHAR, TRACKER_RANK, TRACKER_TAG, MPI_COMM_WORLD));
}

/* The upload thread's function, polls any message recevied with the 
 * UPLOAD_TAG and manages the file transfer 
 * This thread can also receive the terminate procedure init from
 * the tracker that signals it to stop as there's no need to 
 * seed / peer any files as all clients have finished downloading */
void Peer::upload_thread_func() {
    while (1) {
        int size;
        MPI_Status status;
        CHECK_MPI_RET(MPI_Recv(&size, 1, MPI_INT, MPI_ANY_SOURCE, UPLOAD_TAG, MPI_COMM_WORLD, &status));
        int source = status.MPI_SOURCE;

        vector<char> buf(size + 1);
        CHECK_MPI_RET(MPI_Recv(buf.data(), size, MPI_CHAR, source, UPLOAD_TAG, MPI_COMM_WORLD, &status));
        buf[size] = '\0';

        string file_name;
        string segment_hash;
		stringstream ss(buf.data());
		ss >> file_name >> segment_hash;

        cerr << "[Peer " << rank << "]: Received request for segment from file "
             << file_name << " from peer " << source << endl;

        if (file_name == "TERMINATE") {
            break;
        }
		int ack = check_if_segment_is_owned(file_name, segment_hash);
		cerr << "[Peer " << rank << "]: Checked if I got segment " << segment_hash
				<< " for peer " << source << endl;
		CHECK_MPI_RET(MPI_Send(&ack, 1, MPI_INT, source, DOWNLOAD_TAG, MPI_COMM_WORLD));	
	}
    cerr << "[Peer " << rank << "]: Terminating upload thread." << endl;
}

/* Checks if we have that file's segment hash in a mutually exclusive manner 
 * returns an ACK / NACK accordingly */
int Peer::check_if_segment_is_owned(string file_name, string segment_hash) {
	int ack;
	/* Check the owned hashes of the files in a mutually exclusive way for 
	 * synchronizing with the download thread */
	owned_files_mtx.lock();
	/* Check if we have the wanted segment, if yes send ACK, if not send NACK */
	if (find(owned_files[file_name].begin(), owned_files[file_name].end(), segment_hash)
			!= owned_files[file_name].end()) 
	{
		ack = ACK;
	} else {
		ack = !ACK;
		
	}
	owned_files_mtx.unlock();
	return ack;
}

/* Peer's initiate by firstly parsing their respective input file 
 * and storing the file content (segment hashes) of the files for which
 * they will act as seeds and the list of the files-to-download */
void Peer::parse_initial_files() {
    ifstream fin("in" + to_string(rank) + ".txt");
    int segment_nr;
    string file_name;
    string segment_hash;
    int file_count;
    /* Parses files for which this client will act as seed */
    fin >> file_count;
    for (int i = 0; i < file_count; i++) {
        fin >> file_name >> segment_nr;
        for (int j = 1; j <= segment_nr; j++) {
            fin >> segment_hash;
            owned_files[file_name].push_back(segment_hash);
        }
    }

    /* Parses the files that this client will download from other peers / seeds */
    fin >> file_count;
    for (int i = 0; i < file_count; i++) {
        fin >> file_name;
        wanted_files.push_back(file_name);
    }
    fin.close();
}

/* Receives a file's swarm for the tracker
 * The tracker's response will containt a list of peers / seeds
 * associated with that file, but also a list of all the segment
 * hashes of that file.
 * NOTE: The client will NOT use these hashes for "downloading", but
 * will only use them so it knows what segments it needs to request
 * from the peers / seeds */
vector<int> Peer::recv_file_swarm_from_tracker(string file_name) {
    int buf_size;
    CHECK_MPI_RET(
        MPI_Recv(&buf_size, 1, MPI_INT, TRACKER_RANK, TRACKER_TAG, MPI_COMM_WORLD, MPI_STATUS_IGNORE)
    );

    vector<char> response_buf(buf_size + 1);
    CHECK_MPI_RET(
        MPI_Recv(response_buf.data(), buf_size, MPI_CHAR, TRACKER_RANK, TRACKER_TAG, MPI_COMM_WORLD, MPI_STATUS_IGNORE)
    );
    response_buf[buf_size] = '\0';

    /* Parse the swarm */
    vector<int> file_owners;
	stringstream file_owners_str(response_buf.data());
	int owners_count;
	file_owners_str >> owners_count;
	for (int i = 0; i < owners_count; i++) {
		int client_rank;
		file_owners_str >> client_rank;
		file_owners.emplace_back(client_rank);
	}

	/* Get this file's segment hashes if we don't have it already 
		* This is needed because we may request a swarm for a file multiple
		* times, [i.e. the 10 segment rule] */
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

/* Requests a file's swarm from the tracker 
 * Firstly send the action type, then
 * the file name string size, then
 * the file name */
void Peer::req_file_swarm_from_tracker(string file_name) {
    int file_name_size = (int)file_name.size();

    int action = SWARM_REQUEST;
    CHECK_MPI_RET(MPI_Send(&action, 1, MPI_INT, TRACKER_RANK, TRACKER_TAG, MPI_COMM_WORLD));
    CHECK_MPI_RET(MPI_Send(&file_name_size, 1, MPI_INT, TRACKER_RANK, TRACKER_TAG, MPI_COMM_WORLD));
    CHECK_MPI_RET(MPI_Send(file_name.c_str(), file_name_size, MPI_CHAR, TRACKER_RANK, TRACKER_TAG, MPI_COMM_WORLD));
}

/* Sends the initial file hashes to the tracker
 * The client will act as a SEED for these files */
void Peer::send_owned_files_to_tracker() {
    string buffer = to_string(owned_files.size()) + " ";
    for (auto &[file_name, hashes] : owned_files) {
        buffer += file_name + " " + to_string(hashes.size()) + " ";
        for (auto &hash : hashes) {
            buffer += hash + " ";
        }
    }

    int size = (int)buffer.size();
    CHECK_MPI_RET(MPI_Send(&size, 1, MPI_INT, TRACKER_RANK, TRACKER_TAG, MPI_COMM_WORLD));
    CHECK_MPI_RET(MPI_Send(buffer.c_str(), buffer.size(), MPI_CHAR, TRACKER_RANK, TRACKER_TAG, MPI_COMM_WORLD));
}

/* Save all the downloaded files' segment hashes in a file
 * A client downloads all of a file's segments in the order 
 * given by the Tracker se there is no need reassembling them
 * before writing to disk */
void Peer::save_file(string wanted_file_name) {
    ofstream fout("client" + to_string(rank)+ "_" + wanted_file_name, ios::app);
    auto &v = owned_files[wanted_file_name];
    for (int i = 0; i < (int)v.size(); i++) {
        fout << v[i];
        if (i != (int)v.size() - 1) {
            fout << "\n";
        }
    }
    fout.close();
}
