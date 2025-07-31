#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>
#include <pthread.h>
#include <uv.h>
#include <sys/stat.h>
#include <sys/stat.h>
#include <sys/stat.h>

#define NUM_FILES 1000
#define FOLDER "modules/"
#define FILE_PREFIX "file"
#define FILE_SUFFIX ".txt"
#define CREATE_CONTENT "Files Created!\n"
#define OVERWRITE_CONTENT "Files Overwritten!\n"

// A context for each file operation chain (open -> write -> close)
typedef struct
{
    uv_fs_t req;
    uv_buf_t iov;
    char filename[256];
    int file_fd;
} file_op_context_t;

// Global state for the application
static uv_loop_t *loop;
static int outstanding_ops = 0;
static const char *content_to_write;
static size_t content_len;

// Forward declarations for our chained callbacks
void on_close(uv_fs_t *req);
void on_write(uv_fs_t *req);
void on_open(uv_fs_t *req);

// Final callback in the chain for a single file.
// Cleans up resources and decrements the counter.
void on_close(uv_fs_t *req)
{
    file_op_context_t *context = req->data;
    free(context);
    outstanding_ops--;
}

// Called after a file is successfully written to.
// Issues the final "close" request.
void on_write(uv_fs_t *req)
{
    file_op_context_t *context = req->data;
    uv_fs_req_cleanup(req);
    // Now, close the file. The on_close callback will handle cleanup.
    uv_fs_close(loop, &context->req, context->file_fd, on_close);
}

// Called after a file is successfully opened.
// Issues the "write" request.
void on_open(uv_fs_t *req)
{
    file_op_context_t *context = req->data;
    uv_fs_req_cleanup(req);

    if (req->result < 0)
    {
        fprintf(stderr, "Error opening file %s: %s\n", context->filename, uv_strerror((int)req->result));
        free(context);
        outstanding_ops--;
        return;
    }

    context->file_fd = req->result;
    context->iov = uv_buf_init((char *)content_to_write, content_len);
    // Now, write to the file. The on_write callback will be next.
    uv_fs_write(loop, &context->req, context->file_fd, &context->iov, 1, -1, on_write);
}

// A highly optimized integer-to-string function.
static inline char *fast_itoa(int value, char *buffer_end)
{
    *buffer_end = '\0';
    char *p = buffer_end;
    if (value == 0)
    {
        *--p = '0';
        return p;
    }
    do
    {
        *--p = '0' + (value % 10);
        value /= 10;
    } while (value > 0);
    return p;
}

int main()
{
    struct timespec start_time, end_time;
    clock_gettime(CLOCK_MONOTONIC, &start_time);

    loop = uv_default_loop();

    // Ensure the directory exists.
    mkdir(FOLDER, 0755);

    // Check if files already exist to determine which content to use.
    // This is a quick, synchronous check before we go async.
    char first_file_path[512];
    snprintf(first_file_path, sizeof(first_file_path), "%s%s0%s", FOLDER, FILE_PREFIX, FILE_SUFFIX);

    if (access(first_file_path, F_OK) == 0)
    {
        printf("INFO: Files exist. Using ultra-fast 'libuv' overwrite method.\n");
        content_to_write = OVERWRITE_CONTENT;
    }
    else
    {
        printf("INFO: Files do not exist. Using ultra-fast 'libuv' creation method.\n");
        content_to_write = CREATE_CONTENT;
    }
    content_len = strlen(content_to_write);

    printf("Submitting %d file operations to libuv...\n", NUM_FILES);

    const size_t folder_len = strlen(FOLDER);
    const size_t prefix_len = strlen(FILE_PREFIX);
    const size_t suffix_len = strlen(FILE_SUFFIX);

    for (int i = 0; i < NUM_FILES; i++)
    {
        // Create a context for this specific file operation.
        file_op_context_t *context = malloc(sizeof(file_op_context_t));
        context->req.data = context;

        // Build the filename using the fast method.
        char *filename_ptr = context->filename;
        memcpy(filename_ptr, FOLDER, folder_len);
        memcpy(filename_ptr + folder_len, FILE_PREFIX, prefix_len);
        char *num_start_ptr = filename_ptr + folder_len + prefix_len;
        char num_buf[12];
        char *num_str = fast_itoa(i, num_buf + sizeof(num_buf) - 1);
        size_t num_len = (num_buf + sizeof(num_buf) - 1) - num_str;
        memcpy(num_start_ptr, num_str, num_len);
        memcpy(num_start_ptr + num_len, FILE_SUFFIX, suffix_len + 1);

        // Increment our counter of outstanding operations.
        outstanding_ops++;

        // Start the async chain: open -> write -> close
        uv_fs_open(loop, &context->req, context->filename, O_WRONLY | O_CREAT | O_TRUNC, 0644, on_open);
    }

    // Run the event loop until all operations are complete.
    uv_run(loop, UV_RUN_DEFAULT);

    // Clean up the loop.
    uv_loop_close(loop);

    clock_gettime(CLOCK_MONOTONIC, &end_time);
    double time_ms = (end_time.tv_sec - start_time.tv_sec) * 1000.0 +
                     (end_time.tv_nsec - start_time.tv_nsec) / 1000000.0;

    printf("\nFinished all libuv operations on %d files.\n", NUM_FILES);
    printf("Total time taken: %.2f ms\n", time_ms);

    return 0;
}