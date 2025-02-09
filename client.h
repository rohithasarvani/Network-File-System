#ifndef CLIENT_H
#define CLIENT_H
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <signal.h>
#include <sys/wait.h>

#define BUFFER_SIZE 4096

int parse_storage_server_info(const char *input, char *ss_ip, int *ss_port);
// Structure for audio player callback
typedef void (*audio_callback)(const unsigned char* data, long size);
int request_audio_stream(int sock, const char* filename);
void write_completion(int sock) ;
#endif