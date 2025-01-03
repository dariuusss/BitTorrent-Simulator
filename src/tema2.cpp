#include <mpi.h>
#include <pthread.h>
#include <fstream>
#include <cstdlib>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <queue>

using namespace std;

#define TRACKER_RANK 0
#define MAX_FILES 10
#define MAX_FILENAME 15
#define HASH_SIZE 32
#define MAX_CHUNKS 100
#define SWARM_REQUEST_CODE 71 
#define SEGMENT_REQUEST_CODE 72
#define CLIENT_DOWNLOADED_ALL_FILES 73
#define CLIENTS_CAN_START 74
#define CLOSE_UPLOAD_THREAD 75 

MPI_Datatype mpi_char_matrix, mpi_char_line, mpi_tracker_files, mpi_file_swarm, mpi_client_request;

int total_number_of_tasks;

struct tracker_file { // fisierul cum il vede trackerul

    char filename[16];
    int peers_list[11];
    int nr_peers;
    char hashes_list[101][33];
    int nr_hashes;
};

struct client_file { // fisier cum il are clientul; devine owned cand clientul are toate hashurile

    char filename[16];
    int nr_owned_hashes;
    int nr_total_hashes;
    char my_hashes_list[101][33];
};

struct file_swarm {
    int nr_peers;
    int peers_list[11];
    int nr_file_hashes;
};

struct client_request {
    int code;
    char filename[16];
    int hash_index; // doar pentru cand cer segmente, cand cer swarm ignor acest camp
};


unordered_map <string, struct client_file>  my_files; // fisier detinut complet sau partial de clientul curent
queue <string> wanted_files; // ce fisiere vrea; sterg de aici cand ii are toate hashurile

void *download_thread_func(void *arg)
{

    int rank = *(int*) arg;
    
    string current_file; 

    //pe moment consider ca sunt gata doar daca am obtinut bine nr de hashes pentru fiecare fisier, sa vad ca merge

    while(!wanted_files.empty()) {

        current_file = wanted_files.front();

        struct client_request req;
        req.code = SWARM_REQUEST_CODE;
        strcpy(req.filename, current_file.c_str());
        req.hash_index = -1; // nu imi pasa de hash cand cer swarm-ul
        cout << "Clientul " << rank << " a cerut " << current_file << endl;
        MPI_Send(&req, 1, mpi_client_request, 0, 30, MPI_COMM_WORLD);
        struct file_swarm swarm_list;
        MPI_Status status;
        MPI_Recv(&swarm_list, 1, mpi_file_swarm, 0, 30, MPI_COMM_WORLD, &status);
        my_files[current_file].nr_total_hashes = swarm_list.nr_peers;

        cout << "Clientul " << rank << " a obtinut " << current_file << endl;
        wanted_files.pop();
    }

    struct client_request finished_download;
    finished_download.code = CLIENT_DOWNLOADED_ALL_FILES;
    MPI_Send(&finished_download, 1, mpi_client_request, 0, 30, MPI_COMM_WORLD);
    cout << "Clientul " << rank << " a descarcat tot\n";
    return NULL;
}

void *upload_thread_func(void *arg)
{
    
    int rank = *(int*) arg;
    MPI_Status status;
    while(1) {
        struct client_request req;
        MPI_Recv(&req, 1, mpi_client_request, MPI_ANY_SOURCE, 40, MPI_COMM_WORLD, &status);
        if(status.MPI_SOURCE == TRACKER_RANK)
            break;
    }
    return NULL;
}

void tracker(int numtasks, int rank) {

    int nr_complete_procs = 0;
    unordered_map <string,struct tracker_file> files_data;
    MPI_Status status;

    int src;


    while(1) {

        if(nr_complete_procs == numtasks - 1) { // am primit de la toti clientii
            char start_ack[30] = "ACK_YOU_CAN_START_DOWNLOADING";
            for(int i = 1; i < numtasks; i++) 
                MPI_Send(&start_ack, 30, MPI_CHAR, i, 20, MPI_COMM_WORLD);
            cout << "Trackerul a trimis tot" << endl;
            break;
        }

        
        struct tracker_file t;
        MPI_Recv(&t, 1, mpi_tracker_files, MPI_ANY_SOURCE, 10, MPI_COMM_WORLD, &status);
        src = status.MPI_SOURCE;
    
        if(strncmp(t.filename,"JOB_DONE_HERE",13)) {

            if(files_data.find(t.filename) == files_data.end()) // primul seed
                files_data[t.filename] = t;
            else { // alt seed
                int k = files_data[t.filename].nr_peers;
                files_data[t.filename].peers_list[k] = src;
                files_data[t.filename].nr_peers++;
            }
        
        } else {
            nr_complete_procs++;
        }
        
    }

    // PANA AICI AI FACUT INITIALIZAREA, DE AICI PRIMESTI CERERI DE SWARMURI

    nr_complete_procs = 0;
    while(1) {

        if(nr_complete_procs == numtasks - 1) {
            struct client_request close_upload;
            close_upload.code = CLOSE_UPLOAD_THREAD;
            for(int i = 1; i < numtasks; i++)
                MPI_Send(&close_upload, 1, mpi_client_request, i , 40, MPI_COMM_WORLD);
            break;
        }

        MPI_Status status;

        struct client_request client_req;
        struct file_swarm response;
        MPI_Recv(&client_req, 1, mpi_client_request, MPI_ANY_SOURCE, 30, MPI_COMM_WORLD, &status);
        if(client_req.code == CLIENT_DOWNLOADED_ALL_FILES)
            nr_complete_procs++;
        else if(client_req.code == SWARM_REQUEST_CODE) {

            string wanted_file = client_req.filename;
            response.nr_peers = files_data[wanted_file].nr_peers;
            response.nr_file_hashes = files_data[wanted_file].nr_hashes;
            for(int i = 0; i < response.nr_file_hashes; i++)
                response.peers_list[i] = files_data[wanted_file].peers_list[i];
            MPI_Send(&response, 1, mpi_file_swarm, status.MPI_SOURCE, 30, MPI_COMM_WORLD);
        }


    }

    cout << "Trackerul si-a terminat executia\n";

}


void read_file(int rank) {

    //cout << "esti in functia de citire pentru procesul " << rank << endl;

    char filename[30] = "";
    sprintf(filename,"%s%d%s","in",rank,".txt");
    int nr_files;
    int nr_fisiere_dorite;
    char fisier_detinut[16];
    int nr_hashuri;
    char hash_curent[33];
    ifstream fin(filename);
    fin >> nr_files;
    for(int i = 0; i < nr_files; i++) {
        fin >> fisier_detinut;
        fin >> nr_hashuri;
        struct tracker_file tf_to_send;
        struct client_file cf;
        strcpy(tf_to_send.filename,fisier_detinut);
        strcpy(cf.filename,fisier_detinut);
        tf_to_send.nr_hashes = nr_hashuri;
        cf.nr_owned_hashes = cf.nr_total_hashes = nr_hashuri;
        tf_to_send.nr_peers = 1;
        tf_to_send.peers_list[0] = rank;
        for(int j = 0; j < nr_hashuri; j++) {
            fin >> hash_curent;
            strcpy(tf_to_send.hashes_list[j],hash_curent);
            strcpy(cf.my_hashes_list[j],hash_curent);
        }

        MPI_Send(&tf_to_send, 1, mpi_tracker_files, 0, 10, MPI_COMM_WORLD);
        my_files[cf.filename] = cf;

    }

    struct tracker_file final_msg;
    strcpy(final_msg.filename,"JOB_DONE_HERE");
    MPI_Send(&final_msg, 1, mpi_tracker_files, 0, 10, MPI_COMM_WORLD);

    // fisierele pe care le doreste
    fin >> nr_fisiere_dorite;
    char fisier_dorit[16];
    for(int k = 0; k < nr_fisiere_dorite; k++) {
        fin >> fisier_dorit;
        wanted_files.push(fisier_dorit);
        struct client_file cf;
        strcpy(cf.filename,fisier_dorit);
        cf.nr_owned_hashes = 0;
        //INITIAL CAND VREAU UN FISIER PANA SA FAC REQUEST STIU DOAR CUM SE NUMESTE
        // SI CA NU AM NICIUN HASH DIN EL
        my_files[fisier_dorit] = cf;
    }

    fin.close();
}

void peer(int numtasks, int rank) {
    pthread_t download_thread;
    pthread_t upload_thread;
    void *status;
    int r;


    read_file(rank);
    char start_download_ack[35] = "";
    MPI_Recv(&start_download_ack, 30, MPI_CHAR, 0, 20, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
    if(strcmp(start_download_ack,"ACK_YOU_CAN_START_DOWNLOADING"))
        exit(-1);

    cout << "Clientul " << rank << " poate incepe descarcarea\n";

    
    r = pthread_create(&download_thread, NULL, download_thread_func, (void *) &rank);
    if (r) {
        printf("Eroare la crearea thread-ului de download\n");
        exit(-1);
    }

    r = pthread_create(&upload_thread, NULL, upload_thread_func, (void *) &rank);
    if (r) {
        printf("Eroare la crearea thread-ului de upload\n");
        exit(-1);
    }

    r = pthread_join(download_thread, &status);
    if (r) {
        printf("Eroare la asteptarea thread-ului de download\n");
        exit(-1);
    }

    r = pthread_join(upload_thread, &status);
    if (r) {
        printf("Eroare la asteptarea thread-ului de upload\n");
        exit(-1);
    }
    

    //PANA AICI AI FACUT INITIALIZAREA, DE ACUM LUCREZI PE THREADURILE DE DOWNLOAD SI UPLOAD

}

void define_mpi_char_matrix() {

    MPI_Type_contiguous(33, MPI_CHAR, &mpi_char_line);
    MPI_Type_commit(&mpi_char_line);
    MPI_Type_contiguous(101, mpi_char_line, &mpi_char_matrix);
    MPI_Type_commit(&mpi_char_matrix);
}


void define_mpi_tracker_files() {

    int field_sizes[5] = {16, 11, 1, 1, 1};
    MPI_Aint offsets[5];
    MPI_Datatype datatypes[5] = {MPI_CHAR, MPI_INT, MPI_INT, mpi_char_matrix, MPI_INT};

    offsets[0] = offsetof(struct tracker_file, filename);
    offsets[1] = offsetof(struct tracker_file, peers_list);
    offsets[2] = offsetof(struct tracker_file,nr_peers);
    offsets[3] = offsetof(struct tracker_file, hashes_list);
    offsets[4] = offsetof(struct tracker_file, nr_hashes);

    MPI_Type_create_struct(5, field_sizes, offsets, datatypes, &mpi_tracker_files);
    MPI_Type_commit(&mpi_tracker_files);
}

void define_mpi_client_request() {

    int field_sizes[3] = {1,16,1};
    MPI_Aint offsets[3];
    MPI_Datatype datatypes[3] = {MPI_INT, MPI_CHAR, MPI_INT};
    offsets[0] = offsetof(struct client_request,code);
    offsets[1] = offsetof(struct client_request,filename);
    offsets[2] = offsetof(struct client_request, hash_index);

    MPI_Type_create_struct(3,field_sizes,offsets,datatypes,&mpi_client_request);
    MPI_Type_commit(&mpi_client_request);

}


void define_mpi_file_swarm() {
    int field_sizes[3] = {1,11,1};
    MPI_Aint offsets[3];
    MPI_Datatype datatypes[3] = {MPI_INT, MPI_INT, MPI_INT};
    offsets[0] = offsetof(struct file_swarm, nr_peers);
    offsets[1] = offsetof(struct file_swarm, peers_list);
    offsets[2] = offsetof(struct file_swarm, nr_file_hashes);

    MPI_Type_create_struct(3, field_sizes, offsets, datatypes, &mpi_file_swarm);
    MPI_Type_commit(&mpi_file_swarm);
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

    total_number_of_tasks = numtasks;

    define_mpi_char_matrix();
    define_mpi_tracker_files();
    define_mpi_file_swarm();
    define_mpi_client_request();
    
    if (rank == TRACKER_RANK) {
        tracker(numtasks,rank);
    } else {
        peer(numtasks, rank);
    }

    MPI_Type_free(&mpi_char_line);
    MPI_Type_free(&mpi_char_matrix);
    MPI_Type_free(&mpi_tracker_files);
    MPI_Type_free(&mpi_file_swarm);
    MPI_Type_free(&mpi_client_request);
    MPI_Finalize();
}