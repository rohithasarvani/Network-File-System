RUN BACKUP SERVER
- "COMMANDLINE ARGS : <port_where_it_must_run> <backup_directory>" when compiling and running backup.c

NAMING SERVER
- Compile and execute naming_server.c
CLIENT

- Compile and execute NSIP, NSPort, C

STORAGE SERVER
- After compiling in command-line args : NS IP, NS PORT, CLIENT_PORT, BACKUP IP, BACKUP PORT,backup_dest_path, accessible paths

Assumptions
- Each backup session is associated with a unique storage server (SS) ID and Backups are timestamped and organized hierarchically
- The server will recursively create subdirectories as needed during backup
STORAGE SERVER INFO :
storage server information is stored in structs , where i have used HASH TABLES which decreases the time complexity in finding the paths



--> File Server with READ, WRITE, APPEND, and INFO Commands


This program implements a file server that supports basic file operations such as READ, WRITE, APPEND, and INFO. The server communicates with a client using a socket connection. It handles files and directories, providing both synchronous and asynchronous options for writing data.
Features

READ
        For files: Reads the file's contents and sends them to the client.
        For directories: Lists all entries in the directory and sends them to the client.

WRITE : Writes data to a file. Supports synchronous and asynchronous modes based on the data size or the --SYNC flag in the request.

APPEND : Appends data to the end of an existing file.

INFO : Provides detailed information about a file, including its size and permissions.
Error Handling :  Reports errors such as attempting unsupported operations on directories or failing to open files.

Command Details
READ
Command: READ <filename>
    Description: Reads the contents of the specified file or lists the contents if the path points to a directory.
    Error Cases:
        File or directory does not exist.
        Permission issues.
WRITE
    Command: WRITE <filename> <data> [--SYNC]
    Description: Writes the specified data to the file.
        If --SYNC is specified or data size is below a threshold, performs synchronous writing.
        Otherwise, asynchronous writing is used, and an acknowledgment is sent immediately to the client.
    Error Cases:
        Attempting to write to a directory.
        File cannot be opened for writing.
APPEND
    Command: APPEND <filename> <data>
    Description: Appends the given data to the file.
    Error Cases:
        Attempting to append to a directory.
        File cannot be opened for appending.
INFO
    Command: INFO <filename>
    Description: Retrieves information about the file, such as size and permissions.

Create command : it is used to create a file or folder in the storage server containing the path 

CREATE <path> <name> F/D   

The flag F/D indicates whether it is a file or directory

the handle_create_command function in the storage server is responsible for the create operation

assumptions:
1.the name of file/directory doesnt contain space
2.this command deals with only absolute paths

Delete command : it is used to delete a file/folder whose path is given

the handle_delete_command function in the storage server is responsible for delete operation
DELETE <path>
1.this command deals with only absolute paths



1) Assumed , Asynchronous threshold to be 1024
2) im using enum for error codes , error codes are there for most of types of errors
3) To clear log file , press Ctrl+C
4) if a write is not asynchronous then it is synchronous
5) for asynchronous wite i have these functions : (i) in storage server i have async_write_task to do async write then i have send_completion_ack_to_ns to send async write completion ack to ns (ii) in naming server i have , notify_client_of_completion to notify respective client of their async write completion

