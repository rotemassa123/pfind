#include "pfind.h"

#include <stdio.h>
#include <sys/queue.h>
#include <malloc.h>
#include <stdlib.h>

typedef struct node{
    char * value;
    struct node * next;
    struct node * prev;
}Node;

typedef struct queue{
    Node * first;//next to be removed
    Node * last;//last inserted (youngest)
    int len;
}Queue;

int insertToQueue(Queue *queue, void* value){
    Node * node;


    node = malloc(sizeof(Node));

    node->value = value;
    node->next = queue->first;

    queue->last->prev = node;
    queue->last = node;

    (queue->len)++;
    return 0;
}

int removeFromQueue(Queue* queue){
    Node * node;

    node = queue->first;

    queue->first = node->prev;
    queue->first->next = NULL;

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

int searchDirectory(char * directory){

}

int main(int argc, char* argv[]){
    if(argc != 4){}

    char * root_directory = argv[1];
    char * search_term = argv[2];
    int num_of_needed_threads = atoi(argv[3]);

    Queue * queue = initQueue();
    insertToQueue(queue, root_directory);

}