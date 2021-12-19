#include "pfind.h"

#include <stdio.h>
#include <sys/queue.h>
#include <malloc.h>
#include <stdlib.h>
#include <pthread.h>
#include <dirent.h>
#include <string.h>
#include <sys/stat.h>

//--------------------declarations and global variables--------------------

#define DOT 1
#define DIRECTORY 2
#define REGULAR 3

typedef struct node{
    char * value;
    struct node * next;
    struct node * prev;
}Node;

typedef struct queue{
    Node * head;//next to be removed
    Node * tail;//tail inserted (youngest)
    int len;
}Queue;

pthread_mutex_t mutex;
pthread_cond_t is_queue_empty_cv;
int is_queue_empty;
Queue * directory_queue;

//--------------------queue functions--------------------

int insertToQueue(Queue *queue, void* value){
    Node * node;

    node = malloc(sizeof(Node));

    node->value = value;
    node->next = queue->head;

    queue->tail->prev = node;
    queue->tail = node;

    (queue->len)++;
    return 0;
}

int removeFromQueue(Queue* queue){
    Node * node;

    if(queue->len == 1){ is_queue_empty = 1; }

    node = queue->head;

    queue->head = node->prev;
    queue->head->next = NULL;

    free(node->value);
    free(node);

    (queue->len)--;
    return 0;
}

Queue * initQueue(){
    Queue * queue;
    queue = malloc(sizeof(Queue));
    queue->len = 0;
    return queue;
}



//--------------------handle different entry cases (file/dir/dot)--------------------

int handleDirCase(char * path, char * dir){
    strcat(path, dir);
    if(opendir(path) == NULL) {
        printf("Directory %s: Permission denied.\n", path);
        exit(1);
    }

    insertToQueue(directory_queue, path);
}

void handleRegularCase(char * path, char * dir, char * term) {
    if(strstr(dir, term) != NULL){
        strcat(path, dir);
        printf("%s\n", path);
    }
}

//--------------------directory and file helpers--------------------
int isDirectory(const char *path) {
    struct stat statbuf;
    if (stat(path, &statbuf) != 0)
        return 0;
    return S_ISDIR(statbuf.st_mode);
}

int getFileType(char * entry) {
    if(!strcmp(entry, ".") || !strcmp(entry, "..")) { return DOT; }
    else if(isDirectory(entry)) { return DIRECTORY; }
    return REGULAR;
}

int searchDirectory(char * search_term){
    char* path = directory_queue->head->value;
    removeFromQueue(directory_queue);

    DIR *folder;
    struct dirent *entry;
    folder = opendir(path);

    while(entry = readdir(folder))
    {
        switch (getFileType(entry->d_name)) {
            case DOT:
                break;

            case DIRECTORY:
                handleDirCase(path, entry->d_name);
                break;

            case REGULAR:
                handleRegularCase(path, entry->d_name, search_term);
                break;
        }
    }

    closedir(folder);
    return 0;
}

void* activateThread(void * search_term_input){
    char * search_term = (char *) search_term_input;
    pthread_mutex_lock(&mutex);
    while(is_queue_empty == 0)
        pthread_cond_wait(&is_queue_empty_cv, &mutex);

    searchDirectory(search_term);
    return 0;
}


int main(int argc, char* argv[]){
    int rc;
    if(argc != 4){}

    char * root_directory = argv[1];
    char * search_term = argv[2];
    int num_of_needed_threads = atoi(argv[3]);

    pthread_t thread[num_of_needed_threads];
    
    for (long i = 0; i < num_of_needed_threads; i++) {
        printf("Main: creating thread %ld\n", i);
        rc = pthread_create(&thread[i], NULL, activateThread, search_term);
        if (rc) { exit(-1); }
    }

    directory_queue = initQueue();
    insertToQueue(directory_queue, root_directory);
}