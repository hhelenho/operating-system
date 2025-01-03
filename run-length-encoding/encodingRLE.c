/*
Acknowledgements
1. https://www.geeksforgeeks.org/basics-file-handling-c/
2. https://www.geeksforgeeks.org/thread-functions-in-c-c/
3. https://nachtimwald.com/2019/04/12/thread-pool-in-c/ 
4. https://man7.org/linux/man-pages/man3/pthread_create.3.html 
5. https://www.geeksforgeeks.org/queue-in-c/ 
6. https://www.nku.edu/~christensen/ASCII.pdf --> test case d involve null character
7. https://stackoverflow.com/questions/16522858/understanding-of-pthread-cond-wait-and-pthread-cond-signal 
*/
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#define CHUNK_SIZE 4096

typedef struct {
    char *pointer; // only need to save the pointer for mmap
    size_t size;
    int index;
} Task;

typedef struct {
    Task *tasks;
    int max;
    int front;
    int rear;
    int count;
    bool done;
    pthread_mutex_t mutex;
    pthread_cond_t cond_nonempty;
    pthread_cond_t cond_nonfull;
} Queue;


typedef struct {
    Task *tasks;          
    bool *available; // adddtional check if result is sent to result queue   
    int max_tasks;        
    pthread_mutex_t mutex;
    pthread_cond_t cond_task_available;
} ResultQueue;

// initialize variables
Queue task_queue;
ResultQueue result_queue;
int worker_thread_number = 1;
pthread_t *tpool;
int last_task_index = -1;
int total_tasks = 0;

// function declarations
void single_sequential_RLE(FILE *input);
void multiple_sequential_RLE(int argc, char *argv[]);
void print_file(const char *filename);
void parallel_RLE(int argc, char *argv[]);
void initialize_queue(Queue *q, int max);
Task dequeue_task(Queue *queue);
void split_files(int argc, char *argv[], int start_index, Queue *queue);
void destroy_queue(Queue *queue);
void *worker_thread(void *arg);
Task encoding_parallel_RLE(Task task);
void stitching_results(void);
size_t get_total_file_size(int argc, char *argv[], int start_index);
void initialize_result_queue(ResultQueue *rq, int max_tasks);
void destroy_result_queue(ResultQueue *rq);

/*
nyuenc will take an optional command-line option -j jobs, which specifies the number of worker threads. 
(If no such option is provided, it runs sequentially.)
*/
int main(int argc, char *argv[]) {
    bool use_parallel = false;
    for (int i = 1; i < argc; i++) {
        if (strchr(argv[i], 'j') != NULL) {
            use_parallel = true;
            break;
        }
    }
    if (use_parallel) {
        parallel_RLE(argc, argv);
    } 
    else {
        // Single file sequential RLE
        if (argc == 2) {
            FILE *input = fopen(argv[1], "rb");
            single_sequential_RLE(input);
            fclose(input);
        } 
        // Multiple files sequential RLE
        else {
            multiple_sequential_RLE(argc, argv);
        } 
    }
    return 0;
}

// Milestone 1: sequential RLE
// Sequential RLE for multiple files
// If multiple files are passed to nyuenc, 
// they will be concatenated and encoded into a single compressed output
void multiple_sequential_RLE(int argc, char *argv[]) {
    // hold on from writing the last character of the first file
    int lastChar = 0;       
    int lastCharCount = 0;  

    for (int i = 1; i < argc; i++) {
        FILE *input = fopen(argv[i], "rb");
        int count = 1;
        int prev = fgetc(input);
        int curr;

        if (prev == EOF) {
            fclose(input);
            continue;
        }
        // First, check for first character
        if (prev == lastChar) {
            count = lastCharCount + 1;
        } else {
            // Otherwise, output the last character from the previous file
            if (lastChar != 0) {
                fputc(lastChar, stdout);
                fputc(lastCharCount, stdout);
            }
        }

        while ((curr = fgetc(input)) != EOF) {
            // same character
            if (curr == prev) {
                count++;
            // different character
            } else {
                fputc(prev, stdout);
                fputc(count, stdout);
                count = 1;
                prev = curr;
            }
        }
        // reach the last character
        lastChar = prev;
        lastCharCount = count;
        fclose(input);
    }
    // output the last character after all files are processed
    if (lastChar != 0) {
        fputc(lastChar, stdout);
        fputc(lastCharCount, stdout);
    }
    fflush(stdout);
}

// simple Run-length encoding logic
// assume that no character will appear more than 255 times in a row
void single_sequential_RLE(FILE *input) {
    int count = 1;
    int prev = fgetc(input);
    int curr;

    if (prev == EOF) {
        return;
    }
    while ((curr = fgetc(input)) != EOF) {
        // same character
        if (curr == prev) {
            count++;
        // different character
        } else {
            fputc(prev, stdout);
            fputc(count, stdout);
            // reset count and update current
            count = 1;
            prev = curr;
        }
    }
    fputc(prev, stdout);
    fputc(count, stdout);
    fflush(stdout);
}

//////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////////

// Milestone 2: parallel RLE
// Parallel RLE function
// Using mutex for synchronization!
// THIS IS MAIN THREAD
void parallel_RLE(int argc, char *argv[]) {
    int i = 1;
    // job number is arg[3]
    // the file argument starts at arg[4]
    if (argc > 2 && strcmp(argv[1], "-j") == 0) {
        worker_thread_number = atoi(argv[2]);
        i = 3; // jobs number (-j jobs)
    }

    size_t total_file_size = get_total_file_size(argc, argv, i);
    int estimated_tasks = (total_file_size / CHUNK_SIZE) + (total_file_size % CHUNK_SIZE != 0);
    int queue_size = estimated_tasks * 10;

    initialize_queue(&task_queue, queue_size);

    // Main thread splits data
    // Divide the input data logically into fixed-size 4KB chunks
    split_files(argc, argv, i, &task_queue);

    // Main thread creats worker thread pool
    // The number of worker threads is specified by the command-line argument -j jobs.
    initialize_result_queue(&result_queue, total_tasks);

    tpool = malloc(worker_thread_number * sizeof(pthread_t));

    for (int t = 0; t < worker_thread_number; t++) {
        pthread_create(&tpool[t], NULL, worker_thread, &task_queue);
    }

    // Waiting for all enqueue is done
    pthread_mutex_lock(&task_queue.mutex);
    task_queue.done = true;
    pthread_cond_broadcast(&task_queue.cond_nonempty);
    pthread_mutex_unlock(&task_queue.mutex);

    // After submitting all tasks, the main thread should collect the results
    // Main thread stitches results as results are being sent to result queue
    stitching_results();

    // Wait for worker threads to complete
    for (int t = 0; t < worker_thread_number; t++) {
        pthread_join(tpool[t], NULL);
    }
    free(tpool);
    destroy_queue(&task_queue);
    destroy_result_queue(&result_queue);
}

// Split files into chunks and enqueue tasks in task_queue
// Use mmap to help with splitting
/* HINT:
// Open file
int fd = open(argv[1], O_RDONLY);
if (fd == -1)
  handle_error();

// Get file size
struct stat sb;
if (fstat(fd, &sb) == -1)
  handle_error();

// Map file into memory
char *addr = mmap(NULL, sb.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
if (addr == MAP_FAILED)
  handle_error();
  */
void split_files(int argc, char *argv[], int start_index, Queue *task_queue) {
    int task_index = 0;

    for (int i = start_index; i < argc; i++) {
        // Open file
        int fd = open(argv[i], O_RDONLY);
        // Get file size
        struct stat st = {0};
        // Map file into memory
        fstat(fd, &st);
        size_t file_size = st.st_size;

        char *addr = mmap(NULL, file_size, PROT_READ, MAP_PRIVATE, fd, 0);
        if (addr == MAP_FAILED) {
            perror("Error mapping file");
            close(fd);
            continue;
        }
        // Split file into CHUNK_SIZE chunks
        for (size_t offset = 0; offset < file_size; offset += CHUNK_SIZE) {
            Task task;
            /* Recall Task struct: 
            typedef struct {
            char *pointer;
            size_t size;
            int index;
            } Task;
            */
            task.pointer = addr + offset;
            if (offset + CHUNK_SIZE > file_size) {
                task.size = file_size - offset;
            } else {
                task.size = CHUNK_SIZE;
            }
            task.index = task_index++;

            // Assume that the task queue is unbounded - can submit all tasks at once without being blocked
            // Enqueue task to task queue
            pthread_mutex_lock(&task_queue->mutex);
            // Wait if the queue is full
            while (task_queue->count == task_queue->max) { 
                pthread_cond_wait(&task_queue->cond_nonfull, &task_queue->mutex);
            }
            task_queue->rear = (task_queue->rear + 1) % task_queue->max;
            task_queue->tasks[task_queue->rear] = task;
            task_queue->count++;
            pthread_cond_signal(&task_queue->cond_nonempty);
            pthread_mutex_unlock(&task_queue->mutex);
        }
        // close(fd);
    }
    last_task_index = task_index - 1;
    // total number of tasks tracked here to be used for stitching limits later
    total_tasks = task_index;
}

// Worker thread function to process tasks in task_queue and add encoded results to result_queue
void *worker_thread(void *arg) {
    Queue *task_queue = (Queue *)arg;
    while (1) {
        pthread_mutex_lock(&task_queue->mutex);
        while (task_queue->count == 0 && !task_queue->done) {
            pthread_cond_wait(&task_queue->cond_nonempty, &task_queue->mutex);
        }
        if (task_queue->count == 0 && task_queue->done) {
            pthread_mutex_unlock(&task_queue->mutex);
            break;
        }
        // Dequeue task and unlock
        Task task = dequeue_task(task_queue);
        pthread_mutex_unlock(&task_queue->mutex);

        if (task.pointer == NULL || task.size == 0) {
            continue;
        }
        Task encoded_task = encoding_parallel_RLE(task);
        // Store the encoded task in result_queue
        pthread_mutex_lock(&result_queue.mutex);
        result_queue.tasks[encoded_task.index] = encoded_task;
        result_queue.available[encoded_task.index] = true;
        pthread_cond_broadcast(&result_queue.cond_task_available); // Signal that a task is available
        pthread_mutex_unlock(&result_queue.mutex);
    }
    return NULL;
}

// // simple Run-length encoding logic but use Task instead
Task encoding_parallel_RLE(Task task) {
    Task result_task;
    result_task.index = task.index;
    size_t max_encoded_size = task.size * 2;
    result_task.pointer = malloc(max_encoded_size);

    if (result_task.pointer == NULL) {
        result_task.size = 0;
        return result_task;
    }

    char *encoded = result_task.pointer;
    size_t encoded_size = 0;
    char current_char = task.pointer[0];
    int count = 1;

    for (size_t i = 1; i < task.size; i++) {
        // Same character
        if (task.pointer[i] == current_char) {
            count++;
        } else {
            // Different character
            encoded[encoded_size++] = current_char;
            encoded[encoded_size++] = count;
            current_char = task.pointer[i];
            count = 1;
        }
    }
    // Last character and count
    encoded[encoded_size++] = current_char;
    encoded[encoded_size++] = count;

    result_task.size = encoded_size; // update new size for efficient memory allocation
    return result_task;
}

// Stitching function does both job in main thread: collect results & stitches
// Stitching results from the result queue in sequential order
void stitching_results(void) {
    int next_index = 0;
    int last_char = -1;  // Initialize to -1 to indicate "no character yet"
    // Cannot use '\0' because null character must be recorded in bianry as well
    int last_count = 0;

    while (next_index < total_tasks) {
        pthread_mutex_lock(&result_queue.mutex);

        // Wait for next index become available
        while (!result_queue.available[next_index]) {
            pthread_cond_wait(&result_queue.cond_task_available, &result_queue.mutex);
        }
        // Finish waiting here..
        // Get the task and mark it as unavailable
        Task task = result_queue.tasks[next_index];
        result_queue.available[next_index] = false; // Reset availability
        pthread_mutex_unlock(&result_queue.mutex);

        char *data = task.pointer;
        size_t size = task.size;

        for (size_t i = 0; i < size; i += 2) {
            char current_char = data[i];
            int current_count = data[i + 1];

            // Check if this is the first character we're processing
            if (last_char == -1) {
                last_char = current_char;
                last_count = current_count;
                continue;
            }
            // Same character
            if (last_char == current_char) {
                last_count += current_count;
            } else {
                // Different character
                fputc(last_char, stdout);
                fputc(last_count, stdout);
                last_char = current_char;
                last_count = current_count;
            }
        }
        free(task.pointer);
        next_index++;
    }
    // Output last character
    if (last_char != -1) {
        fputc(last_char, stdout);
        fputc(last_count, stdout);
    }
    fflush(stdout);
}


// Initialize a queue
void initialize_queue(Queue *q, int max) {
    q->tasks = malloc(max * sizeof(Task));
    if (!q->tasks) {
        exit(EXIT_FAILURE); // DEBUG CHECKPOINT!!
    }
    q->max = max;
    q->front = 0;
    q->rear = -1;
    q->count = 0;
    q->done = false;
    pthread_mutex_init(&q->mutex, NULL);
    pthread_cond_init(&q->cond_nonempty, NULL);
    pthread_cond_init(&q->cond_nonfull, NULL);
}

// Initialize the result queue
void initialize_result_queue(ResultQueue *rq, int max_tasks) {
    rq->tasks = malloc(max_tasks * sizeof(Task));
    rq->available = malloc(max_tasks * sizeof(bool));
    memset(rq->available, 0, max_tasks * sizeof(bool)); // Initialize all to false
    rq->max_tasks = max_tasks;
    pthread_mutex_init(&rq->mutex, NULL);
    pthread_cond_init(&rq->cond_task_available, NULL);
}

void destroy_result_queue(ResultQueue *rq) {
    free(rq->tasks);
    free(rq->available);
    pthread_mutex_destroy(&rq->mutex);
    pthread_cond_destroy(&rq->cond_task_available);
}

// dequeue return the Task struct so we can access the attributes
Task dequeue_task(Queue *queue) {
    Task task = queue->tasks[queue->front];
    queue->front = (queue->front + 1) % queue->max;
    queue->count--;
    pthread_cond_signal(&queue->cond_nonfull);  // Signal non-full state
    return task;
}

void destroy_queue(Queue *queue) {
    free(queue->tasks);
    pthread_mutex_destroy(&queue->mutex);
    pthread_cond_destroy(&queue->cond_nonempty);
    pthread_cond_destroy(&queue->cond_nonfull);
}

// This function calculates total size of all input files at the beginning to effieciently split into 4KB chunk
size_t get_total_file_size(int argc, char *argv[], int start_index) {
    size_t total_size = 0;
    for (int i = start_index; i < argc; i++) {
        struct stat st;
        if (stat(argv[i], &st) == 0) {
            total_size += st.st_size;
        } else {
            perror("Error getting file size");
        }
    }
    return total_size;
}


/*
Running MakeFile:
make run
*/
