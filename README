# Meaning of Data Types and Variables

## Data Structures

- **struct tracker_file**: Stores file data from the tracker's perspective: number of owners, their identities, file hashes in order, and file names.

- **struct client_file**: Stores a file as the client has it (either completely or partially at a given time): file name, the number of segments the client has, the total number of segments in the complete file, and the list of segments currently held by the client.

- **struct file_swarm**: Used for communication between the client's download thread and the tracker. Messages can be of four types: swarm request, update, notification about a file download, or notification about all files being downloaded. Since a valid swarm must have a positive number of owners (seeds or peers), when sending the request from `download_thread_func` to the tracker, the `nr_peers` field is assigned one of the following codes to identify the message type:
  - `CLIENT_DOWNLOADED_ALL_FILES = -1`
  - `CLIENT_DOWNLOADED_ONE_FILE = -2`
  - `SWARM_REQUEST = -7000`
  - `UPDATE_REQUEST = -7567`

  When the tracker receives a buffer of type `struct file_swarm`, it fills it with the appropriate response data, overwrites the code assigned for message identification, and sends the correct data for download (only for swarm/update requests; messages informing the tracker do not require a response).

- **struct client_request**: Used for segment requests and their corresponding responses (these exchanges occur between a client's download function and another client's upload function) or for a message from the tracker instructing the upload thread to stop. Since the tracker does not download files and thus does not request segments, the only case where the tracker is the source of a message is when the upload thread needs to be stopped. If the source is not the tracker, it is a normal segment request.

- **my_files**: Stores the client's files (which can be fully or partially held at a given time).

- **wanted_files**: A queue storing the names of the files the client wants to download.

## Function Explanations

- **void read_file(int rank)**: Reads data about the files a client fully owns (is a seed) and the files they want. If a file is fully owned, its data is stored by the client and sent to the tracker. If a file is desired, its data is only remembered by the client (initially, just the file name and the fact that the client has no segments of it yet). As it reads data about the files the client is seeding, this information is sent to the tracker. Once all fully owned files have been sent, the client notifies the tracker that the initialization process is complete.

- **void tracker(int numtasks, int rank)**: The tracker operates in two loops: the first is for initialization, and the second handles communication with clients after they begin downloading (clients are allowed to start downloading **only** after receiving approval from the tracker). In loop 1, data is received from clients via `read_file()` until all clients inform the tracker that they have sent all their data. When the tracker has received `numtasks - 1` such notifications, it sends all clients an ACK for "start download" and then exits the initialization loop.

  Loop 2 handles communication with clients after initialization. The tracker can process four types of messages:
  1. Notification that a client has downloaded a file
  2. Notification that a client has downloaded all desired files
  3. Swarm/update request
  4. Notification that all clients have finished downloading all desired files

  For messages of type (1) and (2), the tracker processes them internally (no response to clients needed). For type (3), the tracker simply provides the requested data and adds the client to the file's swarm. When a type (4) message is received, the tracker instructs all clients to close their upload threads and then terminates itself (ending loop 2).

- **void *download_thread_func(void *arg)**: This function handles file downloading for a client. For each desired file, it sends a swarm request, waits for a response, and starts downloading. During the process, every 10 segments, the client sends an update request to the tracker since the swarm may change. Then, the client checks which segments it is missing and starts making requests until it receives the desired segment, repeating this process. To ensure efficient downloading and avoid requesting segments from the same client repeatedly, a random index is generated to determine the request destination. If the desired segment is not received on the first attempt, requests are made in a circular approach through the swarm until the segment is received, at which point the process stops. Once a file has been fully downloaded, the client reconstructs the output file and informs the tracker of the download. Additionally, the client notifies the tracker when all desired files have been downloaded.

- **void *upload_thread_func(void *arg)**: This function handles incoming segment requests from clients. It checks its own version of the requested file, verifies if it can provide the requested segment, and sends an appropriate response to the requesting client. Since the tracker cannot make download requests, the upload thread can only receive a message from the tracker when it needs to shut down, as all clients have completed their downloads.

- **void peer(int numtasks, int rank)**: This function calls the data reading function for a client, then waits to receive an ACK from the tracker to start downloading (no client activity begins before this ACK is received). Once the ACK is received, the client starts its upload and download threads.
