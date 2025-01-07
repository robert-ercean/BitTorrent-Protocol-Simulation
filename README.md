# README

This repository contains a simplified multi-process BitTorrent-like program that uses MPI (Message Passing Interface) for communication.  
It consists of two main parts:

1. **Tracker** (rank 0) – Mediates the swarms, seeds, peers and the overall download flow and completion.  
2. **Peers** (ranks [1, n−1]) – each peer can seed / peer files and download others, exchanging segment data with other peers.

---

## Table of Contents
- [Overview](#overview)
  - [Tracker Overview](#tracker-overview)
  - [Peer Overview](#peer-overview)
- [Build Instructions](#build-instructions)
- [Detailed Workflow](#detailed-workflow)
  - [Tracker Side](#tracker-workflow)
  - [Peer Side](#peer-workflow)
- [Efficiency](#efficiency)
---

## Overview

The code uses MPI for communication among:
- **Rank 0** (the Tracker)
- **All other ranks** (the Peers)

### Tracker Overview

- Receives and registers initial files from all peers (which act as seeds for those files).  
- Acknowledges them so they can start downloading from each other.  
- Enters a loop (in `start_mediating_the_swarms()`) to handle various requests:
  - **SWARM_REQUEST**: When a peer wants to know who has the file.  
  - **PEER_UPDATE**: A peer notifies the tracker that it can now help seed/peer a file.  
  - **SINGLE_FILE_DOWNLOAD_COMPLETED**: A peer has finished one file.  
  - **CLIENT_GOT_ALL_FILES**: A peer has finished *all* wanted files.
- Once all peers finish, the tracker calls `signal_all_seeds_to_terminate()`—sending a **TERMINATE** message so all peers stop uploading.

### Peer Overview

- Each peer reads an input file specifying:
  1. Which files (and their segments) it already owns (i.e., seeding).
  2. Which files it wants to download.
- After sending the owned files to the tracker, it waits for an **acknowledgment** before spawning two threads:
  - A **download thread** that requests file segments from other peers (in a round-robin fashion).
  - An **upload thread** that listens for segment requests from other peers and responds accordingly.
- Once finished with all downloads, the peer notifies the tracker and eventually receives a **TERMINATE** message to end its upload thread.

---

## Detailed Workflow

### Tracker Workflow

1. **Initialization**  
   - **`receive_initial_files_from_clients()`**  
     Receives and parses the files that each peer seeds.  
   - **`acknowledge_initial_files()`**  
     Sends an **ACK** message (`ack = 1`) back to each peer so they can start their workflow (downloading/uploading).
   - **`start_mediating_the_swarms()`**  
     Enters a loop that listens for messages from peers:
     - **SWARM_REQUEST**  
       When a peer asks for the list of seeds/peers for a file, the tracker responds with current owners and the file’s segment hashes.
     - **PEER_UPDATE**  
       A peer updates the tracker that it has begun to seed or partially seed a new file.
     - **SINGLE_FILE_DOWNLOAD_COMPLETED**  
       A peer has finished downloading one of its wanted files and can now serve as a seed for that file.
     - **CLIENT_GOT_ALL_FILES**  
       A peer has finished downloading *all* its wanted files. The tracker increments `clients_done`.  
     The loop continues until all peers (i.e., rank 1..N−1) are done.
   - **`signal_all_seeds_to_terminate()`**  
     Once the tracker detects that every peer finished its downloads, it sends each peer a **TERMINATE** message to gracefully end their upload threads.

2. **File Swarm Management**  
   - Internally, the tracker maintains mappings for:
     - **`file_content[file_name]`** – all segment hashes for that file.
     - **`swarms[file_name]`** – which peers currently have the file (in part or fully).
     - **`file_control_blocks[file_name]`** – a structure that differentiates between “seeds” (fully own the file) vs. “peers” (partially own).

### Peer Workflow

1. **Initialization**  
   - **`parse_initial_files()`**  
     Reads each peer’s input file (`inX.txt`) to discover:
     - **Owned files (seed)** – a list of `(file_name, segment_hashes)`.
     - **Wanted files** – the files to download.
   - **`send_owned_files_to_tracker()`**  
     Tells the tracker which files and segments this peer can seed right away.
   - **`wait_for_initial_ack()`**  
     Blocks until the tracker sends back an **ACK** indicating it has processed the owned-files info and is ready.

2. **Threads**  
   Upon receiving the **ACK**, the peer spawns two threads:
   - **Download Thread** (`download_thread_func()`)  
     - Iterates over each file in the **wanted** list.
     - Requests the swarm from the tracker (`req_file_swarm_from_tracker()` + `recv_file_swarm_from_tracker()`).
     - **Downloads segments** in a round-robin approach from the swarm owners:
       1. Chooses a target peer.
       2. Sends a request for the segment.
       3. Receives **ACK** (segment found) or **NACK** (peer doesn’t have it).
       4. If **ACK**, the segment is appended to `owned_files[...]`.
       5. After every 10 segments, notifies the tracker (`send_peer_update_to_tracker()`) and re-requests the swarm in case new peers joined.
     - Once a file is fully downloaded, the peer:
       1. Sends **SINGLE_FILE_DOWNLOAD_COMPLETED** to the tracker.
       2. Calls `save_file(...)` to write the file to disk.
     - When all wanted files are done, sends **CLIENT_GOT_ALL_FILES** to the tracker.
   - **Upload Thread** (`upload_thread_func()`)  
     - Waits for incoming requests on **UPLOAD_TAG**.
     - If it receives **TERMINATE**, the thread breaks and ends.
     - Otherwise, it parses `(file_name, segment_hash)`, checks if it owns that segment, and sends back:
       - **ACK** (if found in `owned_files[file_name]`).
       - **NACK** (if not found).

3. **Completion**  
   - Once the **download thread** signals `CLIENT_GOT_ALL_FILES`, the tracker, after receiving this signal from all the clients sends **TERMINATE** to each of them.
   - The **upload thread** sees **TERMINATE** and ends.
   - Peer finishes execution once both threads have joined.

--

## Efficiency

- **Round-Robin Approach**  
  - The download thread uses a round-robin approach to request segments from peers.
  - This ensures that no single peer / seed is overwhelmed with requests, and the download is distributed evenly among all peers / seeds of a file.
  - The tracker also updates the swarm list periodically, so the download thread can discover new peers / seeds that have joined the swarm.