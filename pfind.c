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
pthread_mutex_t remove_mutex;
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

int Enqueue(Queue *queue, void* value){
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

int Dequeue(Queue* queue){//removes head
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

int getIndexFromHead(Queue * queue, long thread_index){//start from head and scan to tail untill relevant is found
    int index = 0;
    Node * node = queue->head;
    while((long)node->value != thread_index){
        index++;
        node = node->prev;
    }

    return index;
}

Node * getByIndex(Queue * queue, int index){//index 0 is head
    int i;
    Node* node;
    node = queue->head;
    for(i = 0; i < index; i++)
        node = node->prev;

    return node;
}

Node * removeFromQueueByIndex(Queue * queue, int index){//TODO: needs fixes!
    Node* node;
    node = getByIndex(queue, index);

    if(queue->len == 1){
        queue->head = NULL;
        queue->tail = NULL;
    }

    else if(index == 0){
        queue->head = node->prev;
        queue->head->next = NULL;
    }

    else if(index == queue->len - 1){
        queue->tail = node->next;
        queue->tail->prev = NULL;
    }

    else{
        node->prev->next = node->next;
        node->next->prev = node->prev;
    }

    node->next = NULL;
    node->prev = NULL;

    (queue->len)--;
    return node;
}
//--------------------handle different entry cases (file/dir/dot)--------------------
void addEntryToPath(char* fullpath, char* path, char* entry){
    strcpy(fullpath, path);
    strcat(fullpath, "/");
    strcat(fullpath, entry);
}

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
    Enqueue(directory_queue, fullpath);
    pthread_mutex_unlock(&directory_mutex);
}

void handleRegularCase(char * path, char * file, char * term) {
    char fullpath[PATH_MAX];
    addEntryToPath(fullpath, path, file);
    printf("handling file:%s in path:%s\n", file, path);
    if(strstr(file, term) != NULL){
        printf("FOUND RELEVANT FILE: %s\n", fullpath);
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

void searchDirectory(long thread_id){
    char path[PATH_MAX];
    int index;
    Node * node;

    perror("locking!\n");
    //pthread_mutex_lock(&remove_mutex);
    perror("locked!\n");
    index = getIndexFromHead(thread_queue, thread_id);
    perror("got index!\n");
    node = removeFromQueueByIndex(directory_queue, index);
    perror("got node!\n");
    removeFromQueueByIndex(thread_queue, index);
    perror("removed node frmo queue!\n");
    pthread_mutex_unlock(&remove_mutex);

    strcpy(path, (char *)node->value);
    DIR *dir;
    struct dirent *entry;
    if((dir = opendir(path)) == NULL){ perror("COULDN'T OPEN FUCKING GILAD!\n"); exit(1); }
    printf("thread #%lu started searching dir %s\n", thread_id, path);


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
    printf("inserting thread #%lu into queue!\n", thread_id);
    Enqueue(thread_queue, (void *) &thread_id);
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
    long thread_id = (long)thread_index_item;
    int queue_index;
    num_of_sleeping_threads++;
    perror("thread going to sleep...\n");
    pthread_cond_wait(&start_cond_var, &cond_var_mutex);
    num_of_sleeping_threads--;
    pthread_mutex_unlock(&cond_var_mutex);
    perror("thread awake!\n");
    while(directory_queue->len > 0 || num_of_sleeping_threads + num_of_failed_threads != num_of_threads){
        pthread_mutex_lock(&thread_mutex);
        perror("Enqueued thread!\n");
        Enqueue(thread_queue, (void *)thread_id);
        pthread_mutex_unlock(&thread_mutex);

        pthread_mutex_lock(&cond_var_mutex);
        if(directory_queue->len == 0){//queue is empty
            perror("queue is empty!\n");
            num_of_sleeping_threads++;
            pthread_cond_wait(&conditional_variables_arr[thread_id], &cond_var_mutex);
            if(num_of_sleeping_threads + num_of_failed_threads == num_of_threads){//work done, needs to die.
                pthread_mutex_unlock(&cond_var_mutex);
                pthread_exit(0);
            }
            num_of_sleeping_threads--;
            pthread_mutex_unlock(&cond_var_mutex);
            searchDirectory(thread_id);
        }
        else{//queue is not empty
            perror("queue is not empty!\n");
            pthread_mutex_unlock(&cond_var_mutex);
            pthread_mutex_lock(&remove_mutex);
            queue_index = getIndexFromHead(thread_queue, thread_id);
            if(queue_index >= directory_queue->len){
                num_of_sleeping_threads++;
                perror("waiting for signal from main...\n");
                pthread_cond_wait(&conditional_variables_arr[thread_id], &remove_mutex);
                if(num_of_sleeping_threads + num_of_failed_threads == num_of_threads && directory_queue->len == 0){//work done needs to die!
                    pthread_mutex_unlock(&remove_mutex);
                    pthread_exit(0);
                }
                num_of_sleeping_threads--;
            }
            perror("starting to search dir!\n");
            searchDirectory(thread_id);
        }
    }
    return 0;
}


int main(int argc, char* argv[]){
    pthread_mutex_init(&thread_mutex, NULL);
    pthread_mutex_init(&directory_mutex, NULL);
    pthread_mutex_init(&remove_mutex, NULL);
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

    directory_queue = initQueue();
    thread_queue = initQueue();
    Enqueue(directory_queue, (void *) root_directory);

    for (long i = 0; i < num_of_threads; i++) {
        printf("Main: creating thread %lu\n", i);
        pthread_cond_init(&conditional_variables_arr[i], NULL);
        if ((pthread_create(&threads[i], NULL, activateThread, (void *) i))) { exit(-1); }
    }


    while(num_of_sleeping_threads < num_of_threads){ sleep(1); }
    sleep(1);
    pthread_cond_signal(&start_cond_var);
    printf("signaled all threads to begin working!\n");

    while(num_of_sleeping_threads != num_of_threads - num_of_failed_threads){
        sleep(5);
        printf("IM INSIDE THE WHILE LOOP!!!\n");
        if(thread_queue->len > 0 && directory_queue->len > 0 ){
            thread_index = *(int *)(thread_queue->head->value);
            pthread_cond_signal(&conditional_variables_arr[thread_index]);
            Dequeue(thread_queue);//no need for mutex lock here since only main
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