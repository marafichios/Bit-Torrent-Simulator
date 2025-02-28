# Bit-Torrent-Simulator

    # BitTorrent Protocol Simulation with MPI

This project simulates the BitTorrent protocol using MPI (Message Passing Interface) to enable distributed file sharing. The system involves:
- **Tracker**: Coordinates file sharing between peers.
- **Peers**: Share or download files using multi-threading.

## Structs I used

### `Segment`
```c
typedef struct Segment {
    int index;                // Index of the segment within a file
    char hash[HASH_SIZE + 1]; // Hash value of the segment
    int peers[MAX_PEERS];     // List of peer owning this segment
    int num_peers;            // Number of peers owning this segment
} Segment;

``````


### `File`
```c
typedef struct File {
    int id;                           // Unique file ID
    char filename[MAX_FILENAME];      // File name
    int num_segments;                 // Number of segments
    Segment segments[MAX_CHUNKS];     // Array of segments
    int downloaded_segments[MAX_CHUNKS]; // Downloaded status of segments (0 or 1)
    int num_downloaded_segments;      // Total downloaded segments
} File;

``````

### `PeerData`
```c
typedef struct PeerData {
    File owned_files[MAX_FILES];   // Files owned by the peer
    int num_owned_files;           // Count of owned files
    File wanted_files[MAX_FILES];  // Files the peer wants to download
    int num_wanted_files;          // Count of wanted files
    int completed_files[MAX_FILES]; // 1 if file download is complete
} PeerData;
``````
# Functions Explained

This section details all the functions used in the project, grouped by their purposes: Helper, Tracker, and Client functionalities.

---

## Helper Functions

### `PeerData read_input(int rank)`
- **Purpose**: Reads input files (`in<RANK>.txt`) to initialize the peer's owned and wanted files.
- **Details**:
  - Parses the file for:
    - Number of owned files and their segment hashes.
    - List of wanted files.

### `void write_output(int rank, File* file)`
- **Purpose**: Writes a fully downloaded file into the `client<RANK>_<FILENAME>` file.
- **Details**:
  - Iterates through downloaded segments and writes their hashes into the output file.

### `int select_peer_with_min_load(Segment* segment)`
- **Purpose**: Chooses the least busy peer owning the specified segment.
- **Details**:
  - Compares the `peer_load` of all peers owning the segment.
  - Returns the peer with the minimum load.

### `void receive_file_info(int source)`
- **Purpose**: Receives file ownership and segment details from a peer.
- **Details**:
  - Reads:
    - File name.
    - Number of segments.
    - Segment hashes and their owner.

---

## Tracker Functions

### `void tracker(int numtasks, int rank)`
- **Purpose**: Central coordination function for managing file sharing between peers.
- **Detailed Steps**:
  1. **Receive Ownership Information**:
     - Gathers file ownership and segment details from all peers.
     - Uses `receive_file_info()` for each file.
  2. **Acknowledge Peers**:
     - Sends an `ACK` signal to peers after gathering their data.
  3. **Handle Ownership Requests**:
     - Listens for `OWNERS` signals from peers requesting file details.
     - Responds with segment and peer ownership information via `process_owners_request()`.
  4. **Track Download Completion**:
     - Waits for `DONE` signals from peers.
     - Verifies if all peers are ready and sends the `FINISH` signal.

### `void process_owners_request(int source, const char* filename)`
- **Purpose**: Responds to a peer’s request for file ownership details.
- **Detailed Steps**:
  - Searches the global file list for the requested filename.
  - Sends:
    - Number of segments.
    - Segment details (index, hash, owning peers).

---

## Client Functions

### `void peer(int numtasks, int rank)`
- **Purpose**: Core peer function to handle file uploads and downloads.
- **Detailed Steps**:
  1. **Send File Ownership**:
     - Sends owned file and segment details to the tracker.
  2. **Receive Acknowledgment**:
     - Waits for the tracker to acknowledge the data (`ACK` signal).
  3. **Start Threads**:
     - Creates two threads:
       - **Download thread**: Handles downloading of wanted files.
       - **Upload thread**: Handles requests from other peers.
  4. **Wait for Upload Completion**:
     - Joins the upload thread to ensure all upload operations finish before termination.

### `void* download_thread_func(void* arg)`
- **Purpose**: Manages downloading of wanted files.
- **Detailed Steps**:
  1. Iterates through all wanted files.
  2. For incomplete files:
     - Calls `get_daat_from_file()` to request and download missing segments.
  3. Signals the tracker (`DONE`) after all files are downloaded.

### `void* upload_thread_func(void* arg)`
- **Purpose**: Handles upload requests from other peers.
- **Detailed Steps**:
  1. Waits for `SEGMENT_REQUEST` signals from other peers.
  2. Responds with:
     - The requested file's segment hash.
     - Prints logs for successful transfers.
  3. Exits upon receiving the `FINISH` signal.

### `void get_data_from_file(int rank, File* file, PeerData* peer_data)`
- **Purpose**: Downloads all missing segments of a specific file.
- **Detailed Steps**:
  1. Requests segment details from the tracker using `require_data()`.
  2. Iterates through file segments:
     - For missing segments:
       - Selects a peer using `select_peer_with_min_load()`.
       - Requests and verifies the segment hash.
       - Updates download status upon successful download.
  3. Marks the file as complete if all segments are downloaded.

### `void require_data(int rank, const char* filename, Segment* segments, int* num_segments)`
- **Purpose**: Requests segment details of a file from the tracker.
- **Detailed Steps**:
  1. Sends `OWNERS` signal and filename to the tracker.
  2. Receives:
     - Number of segments.
     - Segment details (index, hash, owning peers).

### Tags
- `1`: Segment request between peers.
- `2`: File ownership transfer and acknowledgment.
- `3`: Peer-to-tracker file info requests.
- `4`: Tracker-to-peer file info responses.
- `5`: Peer-to-peer segment hash transfer.


    
