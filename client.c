#include "client.h"
#include "storage_server.h"
// #include "naming_server.h"
int request_audio_stream(int sock, const char* filename) {

    // Receive file size
    long file_size;
    recv(sock, &file_size, sizeof(file_size), 0);

    // Create a pipe to redirect audio data
    int pipefd[2];
    if (pipe(pipefd) == -1) {
        printf("Error Failed to create pipe (ERROR CODE %d)\n",ERR_PIPE);
        perror("Failed to create pipe");
        return -1;
    }

    // Fork a child process to run the MP3 player
    pid_t pid = fork();
    if (pid == 0) {
        // Child process
        close(pipefd[1]); // Close write end of the pipe
        dup2(pipefd[0], STDIN_FILENO); // Redirect stdin to read end of the pipe
        execlp("mpv", "mpv", "--", "/dev/fd/0", NULL); // Run the MP3 player
        printf("Error Failed to stream audio (ERROR CODE %d)\n",ERR_FAILED_TO_STREAM_AUDIO);
        perror("Failed to execute MP3 player");
        exit(1);
    } else if (pid > 0) {
        // Parent process
        close(pipefd[0]); // Close read end of the pipe

        // Receive and write audio data to the pipe
        unsigned char buffer[CHUNK_SIZE];
        long total_received = 0;
        while (total_received < file_size) {
            ssize_t bytes_received = recv(sock, buffer, CHUNK_SIZE, 0);
            if (bytes_received <= 0) {
                break;
            }

            ssize_t bytes_written = write(pipefd[1], buffer, bytes_received);
            if (bytes_written != bytes_received) {
                perror("Failed to write to pipe");
                break;
            }

            total_received += bytes_received;
        }

        close(pipefd[1]); // Close write end of the pipe
        waitpid(pid, NULL, 0); // Wait for the child process (MP3 player) to finish
    } else {
        perror("Failed to fork child process");
        close(pipefd[0]);
        close(pipefd[1]);
        return -1;
    }
    return 0;
}
int connect_to_storage_server(const char *server_ip, int server_port) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        printf("socket error (ERROR CODE %d)\n",ERR_SOCK);
        perror("Socket creation error");
        return -1;
    }

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(server_port);

    // Convert IP address
    if (inet_pton(AF_INET, server_ip, &server_addr.sin_addr) <= 0) {
        perror("Invalid address");
        close(sock);
        return -1;
    }

    // Connect to server
    if (connect(sock, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        printf("socket connection error (ERROR CODE %d)\n",ERR_SOCK_CONNECT);
        perror("Connection failed");
        close(sock);
        return -1;
    }

    printf("Connected to server at %s:%d\n", server_ip, server_port);
    return sock;
}

void handle_read_response(int sock) {
    char buffer[BUFFER_SIZE];
    memset(buffer, 0, sizeof(buffer));
    int bytes_received;

    printf("Storage Server Response:\n");
    while ((bytes_received = recv(sock, buffer, sizeof(buffer) - 1, 0)) > 0) {
        buffer[bytes_received] = '\0';
        printf("%s", buffer);  // Print received data
        memset(buffer, 0, sizeof(buffer));
    }

    if (bytes_received < 0) {
        perror("Error receiving response");
    }
    printf("\n");
}

// Function to receive response for WRITE command
void handle_write_response(int sock) {
    // printf("hello======\n");
    char buffer[BUFFER_SIZE];
    memset(buffer, 0, sizeof(buffer));

    int bytes_received = recv(sock, buffer, sizeof(buffer) - 1, 0);
    if (bytes_received > 0) {
        buffer[bytes_received] = '\0';
        printf("Write Response: %s\n", buffer);  // Display success or error message
    } else if (bytes_received < 0) {
        perror("Error receiving write response");
    }

}
void handle_create_response(int sock) {
    // printf("hello======\n");
    char buffer[BUFFER_SIZE];
    memset(buffer, 0, sizeof(buffer));

    int bytes_received = recv(sock, buffer, sizeof(buffer) - 1, 0);
    if (bytes_received > 0) {
        buffer[bytes_received] = '\0';
        printf("CREATE Response: %s\n", buffer);  // Display success or error message
    } else if (bytes_received < 0) {
        perror("Error receiving write response");
    }

}
void handle_delete_response(int sock) {
    // printf("hello======\n");
    char buffer[BUFFER_SIZE];
    memset(buffer, 0, sizeof(buffer));

    int bytes_received = recv(sock, buffer, sizeof(buffer) - 1, 0);
    if (bytes_received > 0) {
        buffer[bytes_received] = '\0';
        printf("DELETE Response: %s\n", buffer);  // Display success or error message
    } else if (bytes_received < 0) {
        perror("Error receiving write response");
    }

}
void handle_append_response(int sock) {
    // printf("hello======\n");
    char buffer[BUFFER_SIZE];
    memset(buffer, 0, sizeof(buffer));

    int bytes_received = recv(sock, buffer, sizeof(buffer) - 1, 0);
    if (bytes_received > 0) {
        buffer[bytes_received] = '\0';
        printf("Write Response: %s\n", buffer);  // Display success or error message
    } else if (bytes_received < 0) {
        perror("Error receiving write response");
    }
}

void handle_info_response(int sock) {
    char buffer[BUFFER_SIZE];
    memset(buffer, 0, sizeof(buffer));

    int bytes_received = recv(sock, buffer, sizeof(buffer) - 1, 0);
    if (bytes_received > 0) {
        buffer[bytes_received] = '\0';
        printf("%s\n", buffer);  // Display file information
    } else if (bytes_received < 0) {
        perror("Error receiving info response");
    }

}

int main(int argc, char *argv[]) {
    if (argc != 4) {
        fprintf(stderr, "Usage: %s <IP> <PORT> <C/S-CHAR>\n", argv[0]);
        return EXIT_FAILURE;
    }

    const char *server_ip = argv[1];
    int server_port = atoi(argv[2]);
    char c = argv[3][0];  // Take the character from the command-line argument

    // Create socket
    int client_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (client_socket < 0) {
        printf("socket error (ERROR CODE %d)\n",ERR_SOCK);
        perror("Error creating socket");
        return EXIT_FAILURE;
    }

    // Set up the server address structure
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(server_port);

    // Convert and set the IP address
    if (inet_pton(AF_INET, server_ip, &server_addr.sin_addr) <= 0) {
        perror("Invalid IP address");
        close(client_socket);
        return EXIT_FAILURE;
    }

    // Connect to the server
    if (connect(client_socket, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        printf("socket connection error (ERROR CODE %d)\n",ERR_SOCK_CONNECT);
        perror("Error connecting to server");
        close(client_socket);
        return EXIT_FAILURE;
    }
    printf("Connected to server at %s:%d\n", server_ip, server_port);

    // Send the initial character to the server
    if (send(client_socket, &c, sizeof(c), 0) < 0) {
        printf("socket send error (ERROR CODE %d)\n",ERR_SOCK_SEND);
        perror("Error sending character");
        close(client_socket);
        return EXIT_FAILURE;
    }
    printf("Character '%c' sent to server\n", c);

    // Receive ACK number
    char ack_buffer[50];
    memset(ack_buffer, 0, sizeof(ack_buffer));
    if (recv(client_socket, ack_buffer, sizeof(ack_buffer), 0) < 0) {
        perror("Error receiving ACK");
        close(client_socket);
        return EXIT_FAILURE;
    }
    
    int ack_number;
    sscanf(ack_buffer, "ACK:%d", &ack_number);
    printf("Received acknowledgment number: %d\n", ack_number);

    // Loop to continuously send commands
    char command[BUFFER_SIZE];
    while (1) {
        printf("Enter command (or type 'STOP' to quit): ");
        fgets(command, sizeof(command), stdin);
        if(command[0] == '\n'){
            printf("Empty input try again (Error code %d)\n",ERR_INVALID_COMMAND);
            continue;
        }

        // Remove newline character from input
        size_t len = strlen(command);
        if (command[len - 1] == '\n') {
            command[len - 1] = '\0';
        }
        // Check for exit command
        if (strcmp(command, "STOP") == 0) {
            printf("Exiting...\n");
            break;
        }
        // Send command to the server
        if (send(client_socket, command, strlen(command), 0) < 0) {
            printf("socket send error (ERROR CODE %d)\n",ERR_SOCK_SEND);
            perror("Error sending command");
            break;
        }
        printf("Command '%s' sent to server\n", command);
        if (strncmp(command, "CREATE", 6) == 0 || strncmp(command, "DELETE", 6) == 0) {
            // Expect response directly from the Naming Server
            char response[BUFFER_SIZE];
            memset(response, 0, sizeof(response));
            
            // if (recv(client_socket, response, sizeof(response), 0) < 0) {
            //     perror("Error receiving response for CREATE/DELETE");
            //     break;
            // }
            
            //printf("Response from Naming Server: %s\n", response);
            sleep(3);
            printf("Operation successful");
    
        }else{
        char  message_about_ss[BUFFER_SIZE];
        recv(client_socket, message_about_ss, sizeof(message_about_ss), 0);
        printf("Received message from server: %s\n", message_about_ss);
        if (strstr(message_about_ss, "Path not given") != NULL) {
            printf("Given path is invalid/Path is not given.\n");
            continue;
        }
        char server_ip[50];
        int server_port;
        int res =  parse_storage_server_info(message_about_ss, server_ip, &server_port);
        if(res == -1){
                printf("INVALID \n");
                continue;
        }
        int storage_sock_fd = connect_to_storage_server(server_ip, server_port);
        char handle_msg[50];
        recv(storage_sock_fd, handle_msg, sizeof(handle_msg), 0);
        while(1){
            if(strncmp(handle_msg, "Handling client request", 24) == 0){
                break;
            }
        }
        //printf("Received message from storage server: %s\n", handle_msg);
        //printf("The command is: %s\n", command);
        if(strncmp(command, "STREAM", 6) == 0){
            send(storage_sock_fd, command, strlen(command), 0);
            char * inst = strtok(command, " ");
            char * filename = strtok(NULL, " ");
            request_audio_stream(storage_sock_fd, filename);
        }
        else if (strncmp(command, "READ", 4) == 0) {
            send(storage_sock_fd, command, strlen(command), 0);
            handle_read_response(storage_sock_fd);
            // printf("..\n");
        } else if (strncmp(command, "WRITE", 5) == 0) {
            // Extract filename and data
            // printf("here1\n");
            char buff[BUFFER_SIZE];
            strcpy(buff,command);
            char *data = strchr(buff, ' ') + 1;  // Move to the filename part
            char *filename = data;
            data = strchr(data, ' ');
            
            if (data) {
                *data = '\0';           // Null-terminate the filename
                data = data + 1;        // Move to the data part
            }
            int fg = 0;
            // Check if data is provided
            if (data && strlen(data) > 0) {
                int sync_flag = 1;
                int data_size = strlen(data);
                // printf("%d\n",data_size);
                if(data_size > ASYNC_THRESHOLD){
                    fg = 1;
                }
            }
            send(storage_sock_fd, command, strlen(command), 0);
            handle_write_response(storage_sock_fd);
            if(fg){
                write_completion(client_socket);
            }
            
        } else if (strncmp(command, "INFO", 4) == 0) {
            send(storage_sock_fd, command, strlen(command), 0);
            handle_info_response(storage_sock_fd);
        }
        else if (strncmp(command, "APPEND", 6) == 0) {
            send(storage_sock_fd, command, strlen(command), 0);
            handle_append_response(storage_sock_fd);
        }
        else{
            printf("Invalid command try again (Error code %d)\n",ERR_INVALID_COMMAND);
        }
        // Check if the command is CREATE or DELETE
        
        close(storage_sock_fd);
    }
    }
    // Close the socket
    close(client_socket);
    return 0;
}


int parse_storage_server_info(const char *input, char *ss_ip, int *ss_port) {
    // Parse the input string using sscanf to extract the IP and port
    int result = sscanf(input, "Storage Server IP: %[^,], Port: %d", ss_ip, ss_port);
    
    // Check if parsing was successful (result should be 2)
    if (result != 2) {
        fprintf(stderr, "Error: Failed to parse storage server info\n");
        return -1;
    }
    return 0;
}

void write_completion(int sock) {
    char completion_buffer[BUFFER_SIZE];
    memset(completion_buffer, 0, sizeof(completion_buffer));
    int bytes_received = recv(sock, completion_buffer, sizeof(completion_buffer) - 1, 0);
    // printf("buffr %s\n",completion_buffer);
    if (bytes_received > 0) {
        completion_buffer[bytes_received] = '\0';
        printf("Async Completion Message: %s\n", completion_buffer);
    } else if (bytes_received < 0) {
        perror("Error receiving async completion response");
    } else {
        printf("No completion message received.\n");
    }
    // memset(completion_buffer, 0, sizeof(completion_buffer));
}