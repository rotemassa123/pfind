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
    void * value;
    struct node * next;
    struct node * prev;
}Node;

typedef struct queue{
    Node * head;//next to be removed
    Node * tail;//tail inserted (youngest)
    int len;
}Queue;

typedef struct SearchTerm{
    char * search_term;
    int thread_index;
}SearchTermAndThreadIndex;

pthread_mutex_t mutex;
pthread_cond_t *conditional_variables_arr;
Queue * directory_queue;
Queue * thread_queue;

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

int removeFromQueue(Queue* queue){//removes head
    Node * node;

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

int isQueueEmpty(Queue * queue){ return queue->len == 0;}

//--------------------handle different entry cases (file/dir/dot)--------------------

void handleDirCase(char * path, char * dir){
    strcat(path, dir);
    if(opendir(path) == NULL) {
        printf("Directory %s: Permission denied.\n", path);
        exit(1);
    }

    pthread_mutex_lock(&mutex);
    insertToQueue(directory_queue, path);
    pthread_mutex_unlock(&mutex);
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

int searchDirectory(SearchTermAndThreadIndex * searchTermAndThreadIndex){
    char* path = directory_queue->head->value;

    pthread_mutex_lock(&mutex);
    removeFromQueue(directory_queue);
    pthread_mutex_unlock(&mutex);

    DIR *folder;
    struct dirent *entry;
    folder = opendir(path);

    while((entry = readdir(folder)))
    {
        switch (getFileType(entry->d_name)) {
            case DOT:
                break;

            case DIRECTORY:
                handleDirCase(path, entry->d_name);
                break;

            case REGULAR:
                handleRegularCase(path, entry->d_name, searchTermAndThreadIndex->search_term);
                break;
        }
    }

    closedir(folder);
    pthread_mutex_lock(&mutex);
    insertToQueue(thread_queue, (void *)&searchTermAndThreadIndex->thread_index);
    pthread_mutex_unlock(&mutex);
    return 0;
}

void* activateThread(void* item){
    SearchTermAndThreadIndex * searchTermAndThreadIndex;
    searchTermAndThreadIndex = (SearchTermAndThreadIndex *) item;

    int thread_index = searchTermAndThreadIndex->thread_index;
    while(isQueueEmpty(directory_queue))
        pthread_cond_wait(&conditional_variables_arr[thread_index], &mutex);

    searchDirectory(searchTermAndThreadIndex);
    return 0;
}


int main(int argc, char* argv[]){
    int rc;
    SearchTermAndThreadIndex * searchTermAndThreadIndex;

    if(argc != 4){}

    char * root_directory = argv[1];
    char * search_term = argv[2];
    int num_of_needed_threads = atoi(argv[3]);

    searchTermAndThreadIndex = malloc(sizeof(SearchTermAndThreadIndex));
    searchTermAndThreadIndex->search_term = search_term;

    conditional_variables_arr = calloc(num_of_needed_threads, sizeof(pthread_cond_t));
    pthread_t thread[num_of_needed_threads];
    
    for (int i = 0; i < num_of_needed_threads; i++) {
        printf("Main: creating thread %d\n", i);

        searchTermAndThreadIndex->thread_index = i;
        rc = pthread_create(&thread[i], NULL, activateThread, searchTermAndThreadIndex);
        if (rc) { exit(-1); }
    }

    directory_queue = initQueue();
    insertToQueue(directory_queue, root_directory);
    thread_queue = initQueue();

    while(isQueueEmpty(directory_queue) && thread_queue->len == num_of_needed_threads){
        searchTermAndThreadIndex->thread_index = *(int *)(thread_queue->head->value);
        activateThread(searchTermAndThreadIndex);
    }
}