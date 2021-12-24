#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <dirent.h>
#include <string.h>
#include <sys/stat.h>
#include <stdatomic.h>
#include <limits.h>
#include <unistd.h>

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

int are_all_threads_idle = 0;
pthread_mutex_t cond_var_mutex;
pthread_mutex_t directory_mutex;
pthread_mutex_t thread_mutex;
pthread_cond_t *conditional_variables_arr;
pthread_cond_t start_cond_var;
Queue * directory_queue;
Queue * thread_queue;
char * search_term;
int num_of_threads;
atomic_int num_of_failed_threads = 0;
atomic_int num_of_sleeping_threads = 0;

//--------------------queue functions--------------------

int insertToQueue(Queue *queue, void* value){
    Node * node;

    node = malloc(sizeof(Node));
    node->value = value;
    node->prev = NULL;

    if(queue->len == 0){ queue->head = node; node->next = NULL; }

    else{
        node->next = queue->tail;
        queue->tail->prev = node;
    }

    queue->tail = node;
    (queue->len)++;
    return 0;
}

int removeFromQueue(Queue* queue){//removes head
    Node * node;
    node = queue->head;
    queue->head = node->prev;

    if(queue->len == 1){ queue->tail = NULL; }
    else
        queue->head->next = NULL;

    free(node);

    (queue->len)--;
    return 0;
}

Queue * initQueue(){
    Queue * queue;
    queue = malloc(sizeof(Queue));
    queue->len = 0;
    queue->head = NULL;
    queue->tail = NULL;
    return queue;
}

void addEntryToPath(char* fullpath, char* path, char* entry){
    strcpy(fullpath, path);
    strcat(fullpath, "/");
    strcat(fullpath, entry);
}
//--------------------handle different entry cases (file/dir/dot)--------------------

void handleDirCase(char * path, char * dir){
    char fullpath[PATH_MAX];

    addEntryToPath(fullpath, path, dir);
    strcat(fullpath, "/");

    if(opendir(path) == NULL) {
        printf("Directory %s: Permission denied.\n", fullpath);
        //exit(1);
    }

    pthread_mutex_lock(&directory_mutex);
    printf("inserting path %s to directory queue!\n", fullpath);
    insertToQueue(directory_queue, fullpath);
    pthread_mutex_unlock(&directory_mutex);
}

void handleRegularCase(char * path, char * file, char * term) {
    char fullpath[PATH_MAX];
    addEntryToPath(fullpath, path, file);
    printf("handling file:%s in path:%s\n", file, path);
    if(strstr(file, term) != NULL){
        strcat(path, file);
        printf("FOUND RELEVANT FILE: %s\n", path);
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

void searchDirectory(long thread_index){
    char path[PATH_MAX];


    strcpy(path, (char *)directory_queue->head->value);
    removeFromQueue(directory_queue);
    pthread_mutex_unlock(&directory_mutex);

    DIR *dir;
    struct dirent *entry;
    if((dir = opendir(path)) == NULL){ perror("COULDN'T OPEN FUCKING GILAD!\n"); exit(1); }
    printf("thread #%lu started searching dir %s\n", thread_index, path);


    while((entry = readdir(dir)))
    {
        printf("entry is:%s\n", entry->d_name);
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
    closedir(dir);
    pthread_mutex_lock(&thread_mutex);
    printf("inserting thread #%lu into queue!\n", thread_index);
    insertToQueue(thread_queue, (void *)&thread_index);
    pthread_mutex_unlock(&thread_mutex);
    exit(-1);
}

void threadSleep(long thread_index){
    num_of_sleeping_threads++;
    printf("thread #%lu: going to sleep...\n", thread_index);
    pthread_cond_wait(&conditional_variables_arr[thread_index], &cond_var_mutex);
    printf("thread #%lu: awake!\n", thread_index);
    num_of_sleeping_threads--;
}

void* activateThread(void* thread_index_item){
    long thread_index = (long)thread_index_item;

    printf("thread #%lu is going to sleep!\n", thread_index);
    num_of_sleeping_threads++;
    pthread_cond_wait(&start_cond_var, &cond_var_mutex);
    printf("thread #%lu is awake!\n", thread_index);
    num_of_sleeping_threads--;

    while(are_all_threads_idle == 0){
        pthread_mutex_lock(&directory_mutex);
        if(directory_queue->len == 0){
            pthread_mutex_unlock(&directory_mutex);
            threadSleep(thread_index);
        }
        printf("directory queue->len: %d\n", directory_queue->len);
        searchDirectory(thread_index);
        threadSleep(thread_index);
    }

    return 0;
}


int main(int argc, char* argv[]){
    pthread_mutex_init(&thread_mutex, NULL);
    pthread_mutex_init(&directory_mutex, NULL);
    pthread_mutex_init(&cond_var_mutex, NULL);
    pthread_cond_init(&start_cond_var, NULL);


    int thread_index;
    if(argc != 4){printf("FUFUFUUUUUUUUUUU\n"); exit(1);}

    char root_directory[PATH_MAX];
    char * root_directory_without_backslash = argv[1];
    search_term = argv[2];
    num_of_threads = atoi(argv[3]);

    strcat(root_directory, root_directory_without_backslash);
    strcat(root_directory, "/");

    conditional_variables_arr = calloc(num_of_threads, sizeof(pthread_cond_t));
    pthread_t threads[num_of_threads];

    for (long i = 0; i < num_of_threads; i++) {
        printf("Main: creating thread %lu\n", i);
        pthread_cond_init(&conditional_variables_arr[i], NULL);
        if ((pthread_create(&threads[i], NULL, activateThread, (void *) i))) { exit(-1); }
    }

    directory_queue = initQueue();
    thread_queue = initQueue();
    insertToQueue(directory_queue, (void*) root_directory);

    while(num_of_sleeping_threads < num_of_threads){ sleep(1); }
    pthread_cond_signal(&start_cond_var);
    printf("signaled all threads to begin working!\n");

    while(directory_queue->len > 0 || thread_queue->len != num_of_threads - num_of_failed_threads){
        sleep(5);
        printf("IM INSIDE THE WHILE LOOP!!!\n");
        if(thread_queue->len > 0){
            thread_index = *(int *)(thread_queue->head->value);
            pthread_cond_signal(&conditional_variables_arr[thread_index]);
            removeFromQueue(thread_queue);//no need for mutex lock here since only main
            // thread can remove from thread_queue (meaning, reference the head)
            printf("signaled thread #%d to start working and removed him from queue!\n", thread_index);
        }
    }

    printf("outside :(\n");

    are_all_threads_idle = 1;
    for (int i = 0; i < num_of_threads; i++){
        pthread_cond_destroy(&conditional_variables_arr[i]);
        pthread_join(threads[i], NULL);
    }
}