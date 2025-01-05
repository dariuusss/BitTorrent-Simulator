#include <mpi.h>
#include <pthread.h>
#include <fstream>
#include <iostream>
#include <cstdlib>
#include <ctime>
#include <unordered_map>
#include <queue>

using namespace std;

#define TRACKER_RANK 0
#define MAX_FILES 10
#define MAX_FILENAME 15
#define HASH_SIZE 32
#define MAX_CHUNKS 100
#define SEGMENT_REQUEST_CODE 72
#define CLOSE_UPLOAD_THREAD 73
#define DOES_NOT_HAVE_SEGMENT 74
#define HAS_SEGMENT 75
#define CLIENT_DOWNLOADED_ALL_FILES -1
#define CLIENT_DOWNLOADED_ONE_FILE -2
#define SWARM_REQUEST -7000

MPI_Datatype mpi_char_matrix, mpi_char_line, mpi_tracker_files, mpi_file_swarm, mpi_client_request;

struct tracker_file { // fisierul cum il vede trackerul

    char filename[16];
    int peers_list[11];
    int nr_peers;
    char hashes_list[101][33];
    int nr_hashes;
};

struct client_file { // fisier cum il are clientul

    char filename[16];
    int nr_owned_hashes;
    int nr_total_hashes;
    char my_hashes_list[101][33];
};

struct file_swarm {
    int nr_peers; //daca nu e cerere de swarm / actualizare, prin acest numar voi codifica alte lucruri; cand trimit raspunsul 
    // pun in acest camp numarul de peers, codul e doar pana sa primesc raspuns
    int peers_list[11];
    int nr_hashes; // nr de hashuri ale fisierului
    char filename[16];
};

struct client_request {
    int code; // indica daca am primit segmentul sau nu
    char filename[16]; 
    int hash_index; // al catelea segment il vreau
    char hash[33];
};


unordered_map <string, struct client_file>  my_files; // fisier detinut complet sau partial de clientul curent
queue <string> wanted_files; // ce fisiere vreau; sterg de aici cand am toate hashuril acelui fisier

void *download_thread_func(void *arg)
{

    srand(time(NULL));

    int rank = *(int*) arg;

    struct file_swarm swarm;
    struct client_request client_req;

    while(!wanted_files.empty()) {

        string current_file = wanted_files.front(); // extrag un fisier din coada ca sa ii incep descarcarea

        swarm.nr_peers = SWARM_REQUEST; // asa marchez cerere normala de swarm
        strcpy(swarm.filename, current_file.c_str());
        MPI_Ssend(&swarm, 1, mpi_file_swarm, 0, 3, MPI_COMM_WORLD); //cer swarm
        MPI_Recv(&swarm, 1, mpi_file_swarm, 0, 7, MPI_COMM_WORLD, MPI_STATUS_IGNORE); // primesc swarm

        my_files[current_file].nr_total_hashes = swarm.nr_hashes; // stiu cate hashuri imi trebuie, incep descarcarea acum

        for(int i = 0; i < swarm.nr_hashes; i++) { // pt fiecare hash

            
            if(i % 10 == 0 && i > 0) { // cerere de actualizare la fiecare 10 segmente descarcate cu succes
                swarm.nr_peers = SWARM_REQUEST;
                strcpy(swarm.filename, current_file.c_str());
                MPI_Ssend(&swarm, 1, mpi_file_swarm, 0, 3, MPI_COMM_WORLD); //cer swarm
                MPI_Recv(&swarm, 1, mpi_file_swarm, 0, 7, MPI_COMM_WORLD, MPI_STATUS_IGNORE); // primesc swarm
            }

            
            int dst_index = rand() % swarm.nr_peers; // pentru a evita suprasolicitarea unui client, aleg unul la intamplare
            // si apoi fac cereri in mod circular pana primesc segmentul
            int dst;
            while(1) {

                int dst = swarm.peers_list[dst_index]; 
                client_req.hash_index = i;
                strcpy(client_req.filename,current_file.c_str());
                MPI_Ssend(&client_req, 1, mpi_client_request, dst, 4, MPI_COMM_WORLD);
                MPI_Recv(&client_req, 1, mpi_client_request, dst, 6, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

                if(client_req.code == HAS_SEGMENT) { // am primit segmentul
                    strcpy(my_files[current_file].my_hashes_list[client_req.hash_index],client_req.hash);
                    my_files[current_file].nr_owned_hashes++;
                    break; // nu mai trimit cereri pt acest segment, trec la altul

                } else { // nu am primit segmentul
                    dst_index = (dst_index + 1) % swarm.nr_peers;
                }

            }
            
        }

        swarm.nr_peers = CLIENT_DOWNLOADED_ONE_FILE; //daca ajung aici, inseamna ca am descarcat fisierul
        strcpy(swarm.filename,current_file.c_str());
        MPI_Ssend(&swarm, 1, mpi_file_swarm, 0, 3, MPI_COMM_WORLD); // ii spun trackerului ca am descarcat fisierul
        
        // fac fisierul de output
        char final_file[20] = "";
        sprintf(final_file,"client%d_%s",rank,current_file.c_str());
        ofstream out(final_file);
        out << my_files[current_file].my_hashes_list[0];
        for(int i = 1; i < my_files[current_file].nr_total_hashes; i++)
            out << '\n' << my_files[current_file].my_hashes_list[i];
        out.close();

        wanted_files.pop(); // scot fisierul din coada, intrucat am terminat cu el
    }

    // daca ajung aici s-a terminat while-ul cu coada, deci inseamna ca am descarcat tot
    swarm.nr_peers = CLIENT_DOWNLOADED_ALL_FILES;
    MPI_Ssend(&swarm, 1, mpi_file_swarm, 0, 3, MPI_COMM_WORLD); //anunt trackerul ca am terminat de descarcat tot, 
    // dupa care se inchide threadul de download
    return NULL;
}


void *upload_thread_func(void *arg)
{
    
    int rank = *(int*) arg;
    struct client_request req;
    MPI_Status status;
    while(1) {
        MPI_Recv(&req, 1, mpi_client_request, MPI_ANY_SOURCE, 4, MPI_COMM_WORLD, &status); // primesc cerere de segment sau inchidere upload

        if(status.MPI_SOURCE == TRACKER_RANK) // daca primesc mesaj de la tracker e cerere de inchidere thread, in rest e de cerere de sgement
            break;

        string name = req.filename;


        if(req.hash_index <= my_files[name].nr_owned_hashes - 1) { // daca detin x hashuri dintr-un anumit fisier,
        //atunci pot da ACK pt cereri de segmente cu indici de la 0 la x - 1

            req.code = HAS_SEGMENT;
            strcpy(req.hash,my_files[name].my_hashes_list[req.hash_index]);
        } else {
            req.code = DOES_NOT_HAVE_SEGMENT;
        }

        MPI_Ssend(&req, 1, mpi_client_request, status.MPI_SOURCE, 6, MPI_COMM_WORLD);

    }
    return NULL;
}

void tracker(int numtasks, int rank) {

    int nr_complete_procs = 0;
    unordered_map <string,struct tracker_file> files_data;
    MPI_Status status;

    int src;

    string filename;


    while(1) {

        if(nr_complete_procs == numtasks - 1) { // am primit de la toti clientii fisierele pentru care sunt seeds(faza de initializare)
        // deci opresc initializarea
            char start_ack[30] = "ACK_YOU_CAN_START_DOWNLOADING";
            for(int i = 1; i < numtasks; i++) 
                MPI_Ssend(&start_ack, 30, MPI_CHAR, i, 1, MPI_COMM_WORLD);
            break;
        }

        

        struct tracker_file t;
        MPI_Recv(&t, 1, mpi_tracker_files, MPI_ANY_SOURCE, 2, MPI_COMM_WORLD, &status);
        filename = t.filename;
        src = status.MPI_SOURCE;
    
        if(strncmp(t.filename,"JOB_DONE_HERE",13)) { // e fisier normal

            if(files_data.find(filename) == files_data.end()) // primul seed
                files_data[filename] = t;
            else { // alt seed
                int k = files_data[filename].nr_peers;
                files_data[filename].peers_list[k] = src;
                files_data[filename].nr_peers++;
            }
        
        } else { //un client a terminat faza de initializare si asteapta ACK de start download
            nr_complete_procs++;
        }
        
    }

    //----------------------------------------------------------------------------------------------

    nr_complete_procs = 0;
    struct file_swarm swarm;
    while(1) {

        if(nr_complete_procs == numtasks - 1) { // ALL CLIENTS FINISHED DOWNLOADING
            
            struct client_request client_req;
            client_req.code = CLOSE_UPLOAD_THREAD;
            for(int i = 1; i < numtasks; i++)
                MPI_Ssend(&client_req, 1, mpi_client_request, i, 4, MPI_COMM_WORLD);
            break;
        }

        MPI_Recv(&swarm, 1, mpi_file_swarm, MPI_ANY_SOURCE, 3, MPI_COMM_WORLD, &status);

        if(swarm.nr_peers == CLIENT_DOWNLOADED_ALL_FILES) { // un client a terminat descarcarea tuturor fisierelor
            nr_complete_procs++;

        } else if(swarm.nr_peers == CLIENT_DOWNLOADED_ONE_FILE) { // un client a descarcat un fisier

            string FILENAME = swarm.filename;
            cout << FILENAME << " a fost descarcat de clientul " << status.MPI_SOURCE << endl; 

        } else if(swarm.nr_peers == SWARM_REQUEST) { // cerere normala de swarm / actualizare

            string file_name = swarm.filename;
            swarm.nr_peers = files_data[file_name].nr_peers; //cand trimit raspunsul nu mai am nevoie
            // de codul pt tipul cererii, pun numarul de peers pt fisierul pt care am primit request

            swarm.nr_hashes = files_data[file_name].nr_hashes;
            for(int i = 0; i < swarm.nr_peers; i++)
                swarm.peers_list[i] = files_data[file_name].peers_list[i];
            MPI_Ssend(&swarm, 1, mpi_file_swarm, status.MPI_SOURCE, 7, MPI_COMM_WORLD);  

            //verific dupa daca e in swarm cel ce a facut cererea si daca nu e, il adaug

            int src = status.MPI_SOURCE;
            int flag = 0; //presupun ca nu e in swarm
            for(int i = 0; i < files_data[file_name].nr_peers; i++)
                if(files_data[file_name].peers_list[i] == src) { // e deja in swarm, nu il adaug iar
                    flag = 1;
                    break;
                }

            if(flag == 0) { //daca nu e deja in swarm, il adaug
                int nr_peers = files_data[file_name].nr_peers;
                files_data[file_name].peers_list[nr_peers] = src;
                files_data[file_name].nr_peers++;
            }
            

        }
    }

}


void read_file(int rank) {

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

        MPI_Ssend(&tf_to_send, 1, mpi_tracker_files, 0, 2, MPI_COMM_WORLD);
        my_files[cf.filename] = cf;

    }

    struct tracker_file final_msg;
    strcpy(final_msg.filename,"JOB_DONE_HERE"); // am trimis la tracker toate fisierele pt care sunt seed
    MPI_Ssend(&final_msg, 1, mpi_tracker_files, 0, 2, MPI_COMM_WORLD);

    // fisierele pe care le doresc
    fin >> nr_fisiere_dorite;
    char fisier_dorit[16];
    for(int k = 0; k < nr_fisiere_dorite; k++) {
        fin >> fisier_dorit;
        wanted_files.push(fisier_dorit);
        struct client_file cf2;
        strcpy(cf2.filename,fisier_dorit);
        cf2.nr_owned_hashes = 0;
        // la inceput, cand vreau un fisier si nu il am, stiu doar cum se numeste
        // si ca nu detin niciun segment din el

        my_files[fisier_dorit] = cf2;
    }

    fin.close();
}

void peer(int numtasks, int rank) {
    pthread_t download_thread;
    pthread_t upload_thread;
    void *status;
    int r;


    read_file(rank); // functia care citeste datele si trimite la tracker fisierele
    //pt care clientul este seed

    char start_download_ack[35] = "";
    MPI_Recv(&start_download_ack, 30, MPI_CHAR, 0, 1, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
    if(strcmp(start_download_ack,"ACK_YOU_CAN_START_DOWNLOADING")) // daca nu am primit ACK-ul de start descarcare nu am voie sa continui
        exit(-1);

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

    int field_sizes[4] = {1,16,1,33};
    MPI_Aint offsets[4];
    MPI_Datatype datatypes[4] = {MPI_INT, MPI_CHAR, MPI_INT, MPI_CHAR};
    offsets[0] = offsetof(struct client_request,code);
    offsets[1] = offsetof(struct client_request,filename);
    offsets[2] = offsetof(struct client_request, hash_index);
    offsets[3] = offsetof(struct client_request, hash);

    MPI_Type_create_struct(4,field_sizes,offsets,datatypes,&mpi_client_request);
    MPI_Type_commit(&mpi_client_request);

}


void define_mpi_file_swarm() {
    int field_sizes[4] = {1,11,1,16};
    MPI_Aint offsets[4];
    MPI_Datatype datatypes[4] = {MPI_INT, MPI_INT, MPI_INT, MPI_CHAR};
    offsets[0] = offsetof(struct file_swarm, nr_peers);
    offsets[1] = offsetof(struct file_swarm, peers_list);
    offsets[2] = offsetof(struct file_swarm, nr_hashes);
    offsets[3] = offsetof(struct file_swarm, filename);

    MPI_Type_create_struct(4, field_sizes, offsets, datatypes, &mpi_file_swarm);
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