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

void *download_thread_func(void *arg)
{
    int rank = *(int*) arg;

    return NULL;
}

void *upload_thread_func(void *arg)
{
    int rank = *(int*) arg;

    return NULL;
}

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

unordered_map <string, struct client_file>  my_files; // fisier detinut complet sau partial de clientul curent
queue <string> wanted_files; // ce fisiere vrea; sterg de aici cand ii are toate hashurile


void tracker(int numtasks, int rank,MPI_Datatype &mpi_tracker_files) {

    int nr_complete_procs = 0;
    unordered_map <string,struct tracker_file> files_data;
    vector <struct tracker_file> recv_seed_data(numtasks);
    MPI_Request seed_requests[numtasks];
    MPI_Status status;

    for(int i = 0; i < numtasks; i++) //sa pot primi de oriunde, nu sa astept spre ex 1 sa primeasca tot ca sa trec la 2
        MPI_Irecv(&recv_seed_data[i], 1 ,mpi_tracker_files, i , 0, MPI_COMM_WORLD, &seed_requests[i]);

    int src,received_some_data;

    while(1) {

        if(nr_complete_procs == numtasks - 1) {
            char start_ack[35] = "ACK_YOU_CAN_START_DOWNLOADING";
            for(int i = 1; i < numtasks; i++) 
                MPI_Send(&start_ack, 30, MPI_CHAR, i, 0, MPI_COMM_WORLD);
            cout << "Trackerul a trimis tot" << endl;
            break;
        }


        received_some_data = 0;
        MPI_Testany(numtasks, seed_requests, &src, &received_some_data, &status);
        if(received_some_data) {

            struct tracker_file t;
            t = recv_seed_data[src];

            if(strncmp(t.filename,"JOB_DONE_HERE",13)) {

                if(files_data.find(t.filename) == files_data.end())
                    files_data[t.filename] = t;
                else { // peer nou
                    int k = files_data[t.filename].nr_peers;
                    files_data[t.filename].peers_list[k] = src;
                    files_data[t.filename].nr_peers++;
                }
                MPI_Irecv(&recv_seed_data[src], 1 ,mpi_tracker_files, src , 0, MPI_COMM_WORLD, &seed_requests[src]);
            } else {
                nr_complete_procs++;
            }

        }
    }

    // PANA AICI AI FACUT INITIALIZAREA

   
}


void read_file(int rank, MPI_Datatype &mpi_tracker_files ) {
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

        MPI_Send(&tf_to_send, 1, mpi_tracker_files, 0, 0, MPI_COMM_WORLD);
        my_files[cf.filename] = cf;

    }

    struct tracker_file final_msg;
    strcpy(final_msg.filename,"JOB_DONE_HERE");
    MPI_Send(&final_msg, 1, mpi_tracker_files, 0, 0, MPI_COMM_WORLD);

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
    }

    fin.close();
}

void peer(int numtasks, int rank, MPI_Datatype &mpi_tracker_files) {
    pthread_t download_thread;
    pthread_t upload_thread;
    void *status;
    int r;


    read_file(rank,mpi_tracker_files);
    char start_download_ack[35] = "";
    MPI_Recv(&start_download_ack,30,MPI_CHAR,0,0,MPI_COMM_WORLD,MPI_STATUS_IGNORE);
    if(strcmp(start_download_ack,"ACK_YOU_CAN_START_DOWNLOADING"))
        exit(-1);
    cout << "Procesul " << rank << " a primit acceptul si poate incepe sa descarce" << endl;

    cout << "De asemenea, procesul " << rank << " doreste " << wanted_files.size() << " fisiere, mai exact:\n";
    while(!wanted_files.empty()) {
        cout << "$$$" << wanted_files.front() << "$$$\n";
        wanted_files.pop();
    }


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

    //AICI AI FACUT INITIALIZAREA, DE ACUM LUCREZI PE THREADURILE DE DOWNLOAD SI UPLOAD

}

void define_mpi_char_matrix(MPI_Datatype &mpi_char_matrix, MPI_Datatype &mpi_char_line) {

    MPI_Type_contiguous(33, MPI_CHAR, &mpi_char_line);
    MPI_Type_commit(&mpi_char_line);
    MPI_Type_contiguous(101, mpi_char_line, &mpi_char_matrix);
    MPI_Type_commit(&mpi_char_matrix);
}


void define_mpi_tracker_files(MPI_Datatype &mpi_tracker_files, MPI_Datatype &mpi_char_matrix, MPI_Datatype &mpi_char_line) {

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

    MPI_Datatype mpi_char_matrix, mpi_char_line, mpi_tracker_files;
    define_mpi_char_matrix(mpi_char_matrix,mpi_char_line);
    define_mpi_tracker_files(mpi_tracker_files,mpi_char_matrix,mpi_char_line);

    
    if (rank == TRACKER_RANK) {
        tracker(numtasks, rank, mpi_tracker_files);
    } else {
        peer(numtasks, rank, mpi_tracker_files);
    }

    MPI_Type_free(&mpi_char_line);
    MPI_Type_free(&mpi_char_matrix);
    MPI_Type_free(&mpi_tracker_files);
    MPI_Finalize();
}
