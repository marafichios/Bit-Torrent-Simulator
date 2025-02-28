#include <mpi.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

#define TRACKER_RANK 0
#define MAX_FILES 10
#define MAX_FILENAME 15
#define HASH_SIZE 32
#define MAX_SEGMENTS 100
#define MAX_PEERS 100


//signal definitions
#define OWNERS 11
#define DONE 22
#define ACK 33
#define SEGMENT_REQUEST 44
#define FINISH 55

//struct of a segment from the fle
typedef struct Segment {
    int index;
    char hash[HASH_SIZE + 1];
    int peers[MAX_PEERS];
    int num_peers;
} Segment;

// struct of a file
typedef struct File {
    int id;
    char filename[MAX_FILENAME];
    int num_segments;
    Segment segments[MAX_SEGMENTS];
    int downloaded_segments[MAX_SEGMENTS]; //0 is not downloaded, 1 is downloaded
    int num_downloaded_segments;
} File;

//struct to store information about owned and wanted files
typedef struct PeerData {
    File owned_files[MAX_FILES];
    int num_owned_files;
    File wanted_files[MAX_FILES];
    int num_wanted_files;
    int completed_files[MAX_FILES]; //1 is fully downloaded
} PeerData;

int peer_load[MAX_PEERS];
int files_count = 0;
File files[MAX_FILES];
PeerData peer_data[MAX_PEERS];

//this function reads the input from the file
//and populates the PeerData struct
PeerData read_input(int rank) {
    PeerData peer_data;
    peer_data.num_owned_files = 0;
    peer_data.num_wanted_files = 0;

    char input_filename[MAX_FILENAME];
    snprintf(input_filename, sizeof(input_filename), "in%d.txt", rank);

    FILE* input_file = fopen(input_filename, "r");
    if (!input_file) {
        printf("Peer %d: Error opening file: %s\n", rank, input_filename);
        exit(EXIT_FAILURE);
    }

    fscanf(input_file, "%d", &peer_data.num_owned_files);

    //read the required data from the owned files
    for (int i = 0; i < peer_data.num_owned_files; i++) {
        File* file = &peer_data.owned_files[i];
        fscanf(input_file, "%s %d", file->filename, &file->num_segments);
        file->id = i;
        file->num_downloaded_segments = 0;

        for (int j = 0; j < file->num_segments; j++) {
            Segment* segment = &file->segments[j];
            fscanf(input_file, "%s", segment->hash);
            segment->index = j;
            segment->num_peers = 0;
        }
    }

    fscanf(input_file, "%d", &peer_data.num_wanted_files);

    //read the names for the wanted files
    for (int i = 0; i < peer_data.num_wanted_files; i++) {
        File* file = &peer_data.wanted_files[i];
        fscanf(input_file, "%s", file->filename);
        file->id = i;
        file->num_downloaded_segments = 0;
    }

    fclose(input_file);
    return peer_data;
}

//creates the output files for the 
//files that have all the segments downloaded
void write_output(int rank, File* file) {
    char client_filename[MAX_FILENAME + 10];
    snprintf(client_filename, sizeof(client_filename), "client%d_%s", rank, file->filename);

    FILE* output_file = fopen(client_filename, "w");
    if (!output_file) {
        printf("Peer %d: Error creating file %s\n", rank, client_filename);
        return;
    }

    //write all segments that have been downloaded to the file
    for (int i = 0; i < file->num_segments; i++) {
        if (file->downloaded_segments[i] == 1) {
            fwrite(file->segments[i].hash, sizeof(char), HASH_SIZE, output_file);
            fwrite("\n", sizeof(char), 1, output_file);
        }
    }

    fclose(output_file);
}

//chose the least busy peer that owns a specified segment
int select_peer_with_min_load(Segment* segment) {
    int min_load = INT_MAX;
    int selected_peer = -1;

    for (int j = 0; j < segment->num_peers; j++) {
        int peer = segment->peers[j];
        if (peer_load[peer] < min_load) {
            min_load = peer_load[peer];
            selected_peer = peer;
        }
    }
    return selected_peer;
}

//process the request from the peer and sends all
//the info about the segments of the specified file
void process_owners_request(int source, const char* filename) {
    int file_found = 0;

    for (int i = 0; i < files_count; i++) {
        if (strcmp(files[i].filename, filename) == 0) {
            file_found = 1;

            //send number of segments
            MPI_Send(&files[i].num_segments, 1, MPI_INT, source, 4, MPI_COMM_WORLD);

            //send all known data for each segment
            for (int j = 0; j < files[i].num_segments; j++) {
                Segment* segment = &files[i].segments[j];

                MPI_Send(&segment->index, 1, MPI_INT, source, 4, MPI_COMM_WORLD);
                MPI_Send(segment->hash, HASH_SIZE, MPI_CHAR, source, 4, MPI_COMM_WORLD);
                MPI_Send(&segment->num_peers, 1, MPI_INT, source, 4, MPI_COMM_WORLD);
                MPI_Send(segment->peers, segment->num_peers * sizeof(int), MPI_INT, source, 4, MPI_COMM_WORLD);
            }
            break;
        }
    }

    if (!file_found) {
        int no_segments = 0;
        MPI_Send(&no_segments, 1, MPI_INT, source, 4, MPI_COMM_WORLD);
    }
}


//asks for specific info about a file from the tracker 
void require_data(int rank, const char* filename, Segment* segments, int* num_segments) {
    printf("Peer %d: Requesting file info for %s from tracker\n", rank, filename);

    //send the signal and name of the file to tracker 
    int signal = OWNERS;
    MPI_Send(&signal, 1, MPI_INT, TRACKER_RANK, 3, MPI_COMM_WORLD);
    MPI_Send(filename, MAX_FILENAME, MPI_CHAR, TRACKER_RANK, 3, MPI_COMM_WORLD);

    MPI_Recv(num_segments, 1, MPI_INT, TRACKER_RANK, 4, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

    //receive the needed details for eachs segment of the specified file
    for (int i = 0; i < *num_segments; i++) {
        Segment* segment = &segments[i];

        MPI_Recv(&segment->index, 1, MPI_INT, TRACKER_RANK, 4, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
        MPI_Recv(segment->hash, HASH_SIZE + 1, MPI_CHAR, TRACKER_RANK, 4, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
        MPI_Recv(&segment->num_peers, 1, MPI_INT, TRACKER_RANK, 4, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
        MPI_Recv(segment->peers, segment->num_peers * sizeof(int), MPI_INT, TRACKER_RANK, 4, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
    }
}

//help with the download by requesting segments from the specified file
//from the peers
void get_data_from_file(int rank, File* file, PeerData* peer_data) {
    printf("Peer %d: Managing file %s with %d segments\n", rank, file->filename, file->num_segments);

    require_data(rank, file->filename, file->segments, &file->num_segments);

    for (int i = 0; i < file->num_segments; i++) {
        Segment* segment = &file->segments[i];

        //if i already downloaded i skip this part
        if (file->downloaded_segments[i]) {
            continue;
        }

        int selected_peer = select_peer_with_min_load(segment);

        //if no peer has it skip again
        if (selected_peer == -1) {
            continue;
        }

        //request segment from the selected peer
        int signal = SEGMENT_REQUEST;
        MPI_Send(&signal, 1, MPI_INT, selected_peer, 1, MPI_COMM_WORLD);
        MPI_Send(file->filename, MAX_FILENAME, MPI_CHAR, selected_peer, 1, MPI_COMM_WORLD);
        MPI_Send(&segment->index, 1, MPI_INT, selected_peer, 1, MPI_COMM_WORLD);

        char received_hash[HASH_SIZE + 1];
        MPI_Recv(received_hash, HASH_SIZE + 1, MPI_CHAR, selected_peer, 5, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

        //verify the hash after receiving the segment
        if (strcmp(received_hash, segment->hash) == 0) {
            file->downloaded_segments[i] = 1;
            file->num_downloaded_segments++;
            peer_load[selected_peer]++;
        }
    }

    //if i got all segments that means the file is complete
    if (file->num_downloaded_segments == file->num_segments) {
        printf("Peer %d: File %s has been fully downloaded\n", rank, file->filename);
        write_output(rank, file);
        peer_data->completed_files[file->id] = 1;
    }
}

//helper function for tracker that receives file ownership information
void receive_file_info(int source) {
    File file;

    //receive the file name and number of segments
    MPI_Recv(file.filename, MAX_FILENAME, MPI_CHAR, source, 2, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
    MPI_Recv(&file.num_segments, 1, MPI_INT, source, 2, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

    file.id = files_count;
    file.num_downloaded_segments = 0;

    //get segment info
    for (int k = 0; k < file.num_segments; k++) {
        Segment* segment = &file.segments[k];
        MPI_Recv(segment->hash, HASH_SIZE, MPI_CHAR, source, 2, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

        segment->index = k;
        segment->num_peers = 1;
        segment->peers[0] = source;
    }

    //store the file in the global files array
    files[files_count++] = file;
}



//this one manages the request from peers like an intermediate
void tracker(int numtasks, int rank) {
    int peer_ready[numtasks];
    memset(peer_ready, 0, sizeof(peer_ready));

    for (int i = 1; i < numtasks; i++) {
        int num_owned_files;
        MPI_Recv(&num_owned_files, 1, MPI_INT, i, 2, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

        //get file ownership information from each peer
        for (int j = 0; j < num_owned_files; j++) {
            receive_file_info(i);
        }
    }

    //send ack to all peers
    for (int i = 1; i < numtasks; i++) {
        int signal = ACK;
        MPI_Send(&signal, 1, MPI_INT, i, 2, MPI_COMM_WORLD);
    }

    while (1) {
        int signal;
        MPI_Status status;

        MPI_Recv(&signal, 1, MPI_INT, MPI_ANY_SOURCE, 3, MPI_COMM_WORLD, &status);

        if (signal == OWNERS) {
            char filename[MAX_FILENAME];
            MPI_Recv(filename, MAX_FILENAME, MPI_CHAR, status.MPI_SOURCE, 3, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

            process_owners_request(status.MPI_SOURCE, filename);

        } else if (signal == DONE) {
            //mark peers as ready
            int ready_peer = status.MPI_SOURCE;
            peer_ready[ready_peer] = 1;

            //see if all peers are ready
            int all_ready = 1;
            for (int i = 1; i < numtasks; i++) {
                if (!peer_ready[i]) {
                    all_ready = 0;
                    break;
                }
            }
            //then send the finish signal to all peers
            if (all_ready) {
                int signal = FINISH;
                for (int i = 1; i < numtasks; i++) {
                    MPI_Send(&signal, 1, MPI_INT, i, 1, MPI_COMM_WORLD);
                }

                printf("Tracker: All peers are ready.\n");
                return;
            }
        }
    }
}

//this function keeps checking for the wanted files
//and if there are, gets the data to complete the missing parts
void* download_thread_func(void* arg) {
    int rank = *(int*)arg;
    while (1) {
        int all_files_completed = 1;

        for (int i = 0; i < peer_data[rank].num_wanted_files; i++) {
            File* wanted_file = &peer_data[rank].wanted_files[i];

            //process the segments of the file if its not fully downloaded
            if (peer_data[rank].completed_files[wanted_file->id] == 0) {
                all_files_completed = 0;
                get_data_from_file(rank, wanted_file, &peer_data[rank]);
            }
        }

        if (all_files_completed) {
            break;
        }
    }

    //when all are done, notify the tracker
    int signal = DONE;
    MPI_Send(&signal, 1, MPI_INT, TRACKER_RANK, 3, MPI_COMM_WORLD);

    return NULL;
}

//function for the upload thread
void* upload_thread_func(void* arg) {
    int rank = *(int*)arg;

    while (1) {
        MPI_Status status;
        int signal;

        //wait for signal 
        MPI_Recv(&signal, 1, MPI_INT, MPI_ANY_SOURCE, 1, MPI_COMM_WORLD, &status);

        if (signal == SEGMENT_REQUEST) {
            //handle a segment request 
            char filename[MAX_FILENAME];
            int chunk_index;

            MPI_Recv(filename, MAX_FILENAME, MPI_CHAR, status.MPI_SOURCE, 1, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
            MPI_Recv(&chunk_index, 1, MPI_INT, status.MPI_SOURCE, 1, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

            //find the wanted file and segment and send it back
            for (int i = 0; i < peer_data[rank].num_owned_files; i++) {
                File* file = &peer_data[rank].owned_files[i];

                if (strcmp(file->filename, filename) == 0) {
                    if (chunk_index >= 0 && chunk_index < file->num_segments) {
                        Segment* segment = &file->segments[chunk_index];
                        MPI_Send(segment->hash, HASH_SIZE + 1, MPI_CHAR, status.MPI_SOURCE, 5, MPI_COMM_WORLD);
                    }
                    break;
                }
            }
        } else if (signal == FINISH) {
            //exit the upload thread
            break;
        }
    }

    return NULL;
}

void peer(int numtasks, int rank) {
    pthread_t download_thread;
    pthread_t upload_thread;
    void *status;
    int r;

    //read the input and send the data to the tracker
    peer_data[rank] = read_input(rank);

    //send info for the owned files to the tracker 
    MPI_Send(&peer_data[rank].num_owned_files, 1, MPI_INT, TRACKER_RANK, 2, MPI_COMM_WORLD);

    for (int i = 0; i < peer_data[rank].num_owned_files; i++) {
        File* file = &peer_data[rank].owned_files[i];
        MPI_Send(file->filename, MAX_FILENAME, MPI_CHAR, TRACKER_RANK, 2, MPI_COMM_WORLD);
        MPI_Send(&file->num_segments, 1, MPI_INT, TRACKER_RANK, 2, MPI_COMM_WORLD);

        for (int j = 0; j < file->num_segments; j++) {
            Segment* segment = &file->segments[j];
            MPI_Send(segment->hash, HASH_SIZE, MPI_CHAR, TRACKER_RANK, 2, MPI_COMM_WORLD);
        }
    }

    //wait for ack from tracker
    int signal;
    MPI_Recv(&signal, 1, MPI_INT, TRACKER_RANK, 2, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

    if (signal != ACK) {
        printf("Peer %d: ACK not received\n", rank);
        exit(-1);
    } else {
        printf("Peer %d: ACK received\n", rank);
    }

    r = pthread_create(&download_thread, NULL, download_thread_func, (void *)&rank);
    if (r) {
        printf("Eroare la crearea thread-ului de download\n");
        exit(-1);
    }

    r = pthread_create(&upload_thread, NULL, upload_thread_func, (void *)&rank);
    if (r) {
        printf("Eroare la crearea thread-ului de upload\n");
        exit(-1);
    }

    r = pthread_join(upload_thread, &status);
    if (r) {
        printf("Eroare la asteptarea thread-ului de upload\n");
        exit(-1);
    }
}
 
int main (int argc, char *argv[]) {
    int numtasks, rank;
 
    int provided;
    MPI_Init_thread(&argc, &argv, MPI_THREAD_MULTIPLE, &provided);
    if (provided < MPI_THREAD_MULTIPLE) {
        fprintf(stderr, "MPI nu are suport pentru multi-threading\n");
        exit(-1);
    }
    MPI_Comm_size(MPI_COMM_WORLD, &numtasks);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);

    if (rank == TRACKER_RANK) {
        tracker(numtasks, rank);
    } else {
        peer(numtasks, rank);
    }

    MPI_Finalize();
}
