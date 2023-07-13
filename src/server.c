#if defined(__linux__) || defined(__bsd__) || defined(__sunos__)
#include <uv.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>
#endif

#include "server.h"

//------------------------------------------------------------------------------
// Internal functions
//------------------------------------------------------------------------------

/**
 * @defgroup ThreadFunctions
 *
 * @brief Program thread organization
 *
 * There are two thread functions: the network thread (main thread) and work
 * pool threads.
 */

/**
 * @brief The entry point for a worker thread.
 *
 * This function handles the processing of tasks by worker threads.
 * It loops continuously, taking tasks from the queue and processing them.
 *
 * @param arg A void pointer, typically to the server object,
 *        used to pass data into the thread.
 *
 * @ingroup ThreadFunctions
 */
static void worker_thread(void* arg);

/**
 * @brief Callback function executed by the main thread in response to async signals.
 *
 * This function is responsible for handling asynchronous events in the main thread,
 * such as sending out responses prepared by the worker threads.
 *
 * @param handle A pointer to the uv_async_t handle that triggered the callback.
 *
 * @ingroup ThreadFunctions
 */
static void main_thread(uv_async_t* handle);




/**
 * @defgroup ServerFunctions
 *
 * @brief Functions that support server operation
 *
 */

/**
 * @brief Initiates the server shutdown process.
 *
 * It signals all worker threads to stop processing new tasks and
 * to finish any tasks they are currently processing.
 *
 * @param server The server that needs to be shutdown.
 *
 * @ingroup ServerFunctions
 */
static void svr_shutdown(vrtql_svr* server);

/**
 * @brief Callback for new client connection.
 *
 * This function is invoked when a new client successfully connects to the server.
 *
 * @param server The server to which the client connected.
 * @param status The status of the connection.
 *
 * @ingroup ServerFunctions
 */
static void svr_on_connect(uv_stream_t* server, int status);

/**
 * @brief Callback for buffer reallocation.
 *
 * This function is invoked when a handle requires a buffer reallocation.
 *
 * @param handle The handle requiring reallocation.
 * @param size The size of the buffer to be allocated.
 * @param buf The buffer.
 *
 * @ingroup ServerFunctions
 */
static void svr_on_realloc(uv_handle_t* handle, size_t size, uv_buf_t* buf);

/**
 * @brief Callback for handle closure.
 *
 * This function is invoked when a handle is closed.
 *
 * @param handle The handle that was closed.
 *
 * @ingroup ServerFunctions

 */
static void svr_on_close(uv_handle_t* handle);

/**
 * @brief Callback for reading data.
 *
 * This function is invoked when data is read from a client.
 *
 * @param client The client from which data was read.
 * @param size The number of bytes read.
 * @param buf The buffer containing the data.
 *
 * @ingroup ServerFunctions
 */
static void svr_on_read(uv_stream_t* client, ssize_t size, const uv_buf_t* buf);

/**
 * @brief Callback for write completion.
 *
 * This function is invoked when a write operation to a client completes.
 *
 * @param req The write request.
 * @param status The status of the write operation.
 *
 * @ingroup ServerFunctions
 */
static void svr_on_write_complete(uv_write_t* req, int status);

/**
 * @defgroup Connection Functions
 *
 * @brief Functions that support connection operation
 *
 * Connections always refer to client-side connections, on the server. They are
 * active connections established within the main UV loop.
 *
 * @ingroup ServerFunctions
 */

/**
 * @brief Creates a new server connection.
 *
 * @param s The server for the connection.
 * @param c The client for the connection.
 * @return A new server connection.
 *
 * @ingroup ServerFunctions
 */
static vrtql_svr_cnx* svr_cnx_new(vrtql_svr* s, uv_stream_t* c);

/**
 * @brief Frees a server connection.
 *
 * @param c The connection to be freed.
 *
 * @ingroup ServerFunctions
 */
static void svr_cnx_free(vrtql_svr_cnx* c);

/**
 * @brief Callback for client connection.
 *
 * This function is triggered when a new client connection is established.
 * It is responsible for processing any steps necessary at the start of a connection.
 *
 * @param c The connection structure representing the client that has connected.
 *
 * @ingroup ServerFunctions
 */
static void svr_client_connect(vrtql_svr_cnx* c);

/**
 * @brief Callback for client disconnection.
 *
 * This function is triggered when a client connection is terminated. It is
 * responsible for processing any cleanup or other steps necessary at the end of
 * a connection.
 *
 * @param c The connection structure representing the client that has disconnected.
 *
 * @ingroup ServerFunctions
 */
static void svr_client_disconnect(vrtql_svr_cnx* c);

/**
 * @brief Callback for client read operations.
 *
 * This function is triggered when data is read from a client connection. It is
 * responsible for processing the received data.
 *
 * @param c The connection structure representing the client that has sent the data.
 * @param size The size of the data that was read.
 * @param buf The buffer containing the data that was read.
 *
 * @ingroup ServerFunctions
 */
static void svr_client_read(vrtql_svr_cnx* c, ssize_t size, const uv_buf_t* buf);

/**
 * @brief Callback for worker thread task processing.
 *
 * This function is triggered to process a task within a worker thread. It is
 * responsible for handling the actual computation or other work associated with
 * the task.
 *
 * @param server The server instance where the task processing is happening.
 * @param req The incoming request to process.
 *
 * @ingroup ServerFunctions
 */
static void svr_client_process(vrtql_svr* server, vrtql_svr_task* req);




/**
 * @defgroup ConnectionMap
 *
 * @brief Functions that provide access to the connection map
 *
 * The connection map tracks all active connections. These functions simplify
 * the map API and include memory management and other server-specific
 * functionality where appropriate.
 *
 * @ingroup ConnectionMap
 */

/**
 * @brief Get the connection corresponding to a client from the connection map.
 *
 * @param map The connection map.
 * @param key The client (used as key in the map).
 * @return The corresponding server connection.
 *
 * @ingroup ConnectionMap
 */
static vrtql_svr_cnx* svr_cnx_map_get(vrtql_svr_cnx_map* map, uv_stream_t* key);

/**
 * @brief Set a connection for a client in the connection map.
 *
 * @param map The connection map.
 * @param key The client (used as key in the map).
 * @param value The server connection.
 * @return Success or failure status.
 *
 * @ingroup ConnectionMap
 */
static int8_t svr_cnx_map_set( vrtql_svr_cnx_map* map,
                               uv_stream_t* key,
                               vrtql_svr_cnx* value);

/**
 * @brief Remove a client's connection from the connection map.
 *
 * @param map The connection map.
 * @param key The client (used as key in the map).
 *
 * @ingroup ConnectionMap
 */
static void svr_cnx_map_remove(vrtql_svr_cnx_map* map, uv_stream_t* key);

/**
 * @brief Clear all connections from the connection map.
 *
 * @param map The connection map.
 *
 * @ingroup ConnectionMap
 */
static void svr_cnx_map_clear(vrtql_svr_cnx_map* map);

/**
 * @defgroup QueueGroup
 *
 * @brief Queue functions which bridge the network thread and workers
 *
 * The network thread and worker threads pass data via queues. These queues
 * contain vrtql_svr_task instances. There is a requests queue which passes
 * tasks from the network thread to workers for processing. There is a response
 * queue that passed tasks from worker queues to the network thread. When passed
 * from network thread to work, tasks take form of incoming data from the
 * client. When passed from the worker threads the the networking thread, tasks
 * take the form of outgoing data to be sent back to the client.
 *
 * Queues have built-in synchronization mechanisms that allow the tasks to be
 * safely passed between threads, as well as ways to gracefull put waiting
 * threads to sleep until data arrives for processing.
 */

/**
 * @brief Initializes a server queue.
 *
 * This function sets up the provided queue with the given capacity. It also
 * initializes the synchronization mechanisms associated with the queue.
 *
 * @param queue Pointer to the server queue to be initialized.
 * @param capacity The maximum capacity of the queue.
 *
 * @ingroup QueueGroup
 */
void queue_init(vrtql_svr_queue* queue, int capacity);

/**
 * @brief Destroys a server queue.
 *
 * This function cleans up the resources associated with the provided queue.
 * It also handles the synchronization mechanisms associated with the queue.
 *
 * @param queue Pointer to the server queue to be destroyed.
 *
 * @ingroup QueueGroup
 */
void queue_destroy(vrtql_svr_queue* queue);

/**
 * @brief Pushes a task to the server queue.
 *
 * This function adds a task to the end of the provided queue.
 * It also handles the necessary synchronization to ensure thread safety.
 *
 * @param queue Pointer to the server queue.
 * @param task Task to be added to the queue.
 *
 * @ingroup QueueGroup
 */
void queue_push(vrtql_svr_queue* queue, vrtql_svr_task* task);

/**
 * @brief Pops a task from the server queue.
 *
 * This function removes and returns a task from the front of the provided queue.
 * It also handles the necessary synchronization to ensure thread safety.
 *
 * @param queue Pointer to the server queue.
 * @return A task from the front of the queue.
 *
 * @ingroup QueueGroup
 */
vrtql_svr_task* queue_pop(vrtql_svr_queue* queue);

/**
 * @brief Checks if the server queue is empty.
 *
 * This function checks whether the provided queue is empty.
 * It also handles the necessary synchronization to ensure thread safety.
 *
 * @param queue Pointer to the server queue.
 * @return True if the queue is empty, false otherwise.
 *
 * @ingroup QueueGroup
 */
bool queue_empty(vrtql_svr_queue* queue);

//------------------------------------------------------------------------------
// Threads
//------------------------------------------------------------------------------

/**
 * The worker_thread() implements the work pool. It is what each worker thread
 * runs. It handles incoming tasks from clients, processes them and returns data
 * back to the main network thread to be send back to the client.
 *
 * @param arg The server structure as void pointer.
 */
void worker_thread(void* arg)
{
    vrtql_svr* server = (vrtql_svr*)arg;

    while (true)
    {
        //> Wait for arrival

        // This will put the thread to sleep on a condition variable until
        // something arrives in queue.
        vrtql_svr_task* request = queue_pop(&server->requests);

        // If there's no request (null request), check the server's state
        if (request == NULL)
        {
            // If server is in halting state, return
            if (server->state == VS_HALTING)
            {
                if (server->trace)
                {
                    vrtql_trace(VL_INFO, "worker_thread shutdown");
                }

                return;
            }
            else
            {
                // If not halting, skip to the next iteration of the loop
                continue;
            }
        }

        if (server->trace)
        {
            vrtql_trace(VL_INFO, "worker_thread(enter) %p", request->client);
        }

        server->process(server, request);
    }
}

/**
 * The main_thread() function that handles outgoing network I/O from worker
 * threads back to clients. It runs within the main UV loop. Data is passed in
 * the form of vrtql_svr_task instances. Worker threads wake up the main UV loop
 * by calling uv_async_send(&server->wakeup) which in turn calls this loop to
 * check the server.responses queue. It drains this queue and returns control
 * back to the main UV loop which sleeps on client connections.
 */
void main_thread(uv_async_t* handle)
{
    vrtql_svr* server = (vrtql_svr*)handle->data;

    if (server->state == VS_HALTING)
    {
        if (server->trace)
        {
            vrtql_trace(VL_INFO, "main_thread(): stop");
        }

        // Join worker threads before cleaning up libuv as they must exit their
        // respective mutexes and condition variables.
        for (int i = 0; i < server->pool_size; i++)
        {
            uv_thread_join(&server->threads[i]);
        }

        // Cleanup libuv and stop the loop
        svr_shutdown(server);

        return;
    }

    while (!queue_empty(&server->responses))
    {
        vrtql_svr_task* task = queue_pop(&server->responses);
        uv_buf_t buf         = uv_buf_init(task->data, strlen(task->data));
        uv_write_t* req      = (uv_write_t*)malloc(sizeof(uv_write_t));

        if(req == NULL)
        {
            fprintf(stderr, "Failed to allocate memory for write data\n");
            exit(EXIT_FAILURE);
        }

        req->data = task;
        uv_write(req, task->client, &buf, 1, svr_on_write_complete);
    }
}

//------------------------------------------------------------------------------
// Server API
//------------------------------------------------------------------------------

vrtql_svr_task* vrtql_svr_task_new(uv_stream_t* c, size_t size, ucstr data)
{
    vrtql_svr_task* task;
    task = (vrtql_svr_task*)vrtql.malloc(sizeof(vrtql_svr_task));

    task->client = c;
    task->size   = size;
    task->data   = data;

    return task;
}

void vrtql_svr_task_free(vrtql_svr_task* t)
{
    if (t != NULL)
    {
        free(t->data);
        free(t);
    }
}

vrtql_svr* vrtql_svr_new(int threads)
{
    vrtql_svr* server     = malloc(sizeof(vrtql_svr));
    server->threads       = malloc(sizeof(uv_thread_t) * threads);
    server->pool_size     = threads;
    server->trace         = 0;
    server->on_connect    = svr_client_connect;
    server->on_disconnect = svr_client_disconnect;
    server->on_read       = svr_client_read;
    server->process       = svr_client_process;
    server->backlog       = 128;
    server->loop          = (uv_loop_t*)malloc(sizeof(uv_loop_t));

    uv_loop_init(server->loop);
    sc_map_init_64v(&server->cnxs, 0, 0);
    queue_init(&server->requests, 100);
    queue_init(&server->responses, 100);
    uv_mutex_init(&server->mutex);

    server->wakeup.data = server;
    uv_async_init(server->loop, &server->wakeup, main_thread);

    return server;
}

void vrtql_svr_free(vrtql_svr* server)
{
    if (server == NULL)
    {
        return;
    }

    if (server->state == VS_RUNNING)
    {
        vrtql_svr_stop(server);
    }

    svr_shutdown(server);
    free(server->threads);
    free(server->loop);

    svr_cnx_map_clear(&server->cnxs);
    sc_map_term_64v(&server->cnxs);

    free(server);
}

int vrtql_svr_send(vrtql_svr* server, vrtql_svr_task* task)
{
    queue_push(&server->responses, task);

    // Notify event loop about the new response
    uv_async_send(&server->wakeup);
}

int vrtql_svr_run(vrtql_svr* server, cstr host, int port)
{
    // Array to hold the thread ids

    for (int i = 0; i < server->pool_size; i++)
    {
        uv_thread_create(&server->threads[i], worker_thread, server);
    }

    uv_tcp_t socket;
    uv_tcp_init(server->loop, &socket);
    socket.data = server;

    struct sockaddr_in addr;
    uv_ip4_addr("0.0.0.0", port, &addr);

    uv_tcp_bind(&socket, (const struct sockaddr*)&addr, 0);

    int r = uv_listen((uv_stream_t*)&socket, server->backlog, svr_on_connect);

    if (r)
    {
        fprintf(stderr, "Listen error %s\n", uv_strerror(r));
        return -1;
    }

    uv_run(server->loop, UV_RUN_DEFAULT);

    if (server->trace)
    {
        vrtql_trace(VL_INFO, "vrtql_svr_run(): stop");
    }

    server->state = VS_HALTED;

    return 0;
}

void vrtql_svr_trace(vrtql_svr* server, int flag)
{
    server->trace           = flag;
    server->requests.trace  = flag;
    server->responses.trace = flag;
}

void vrtql_svr_stop(vrtql_svr* server)
{
    // Set shutdown flags
    server->state           = VS_HALTING;
    server->requests.state  = VS_HALTING;
    server->responses.state = VS_HALTING;

    // Wakeup all worker threads
    if (server->trace)
    {
        vrtql_trace(VL_INFO, "vrtql_svr_stop(): shutdown workers");
    }

    uv_mutex_lock(&server->requests.mutex);
    uv_cond_broadcast(&server->requests.cond);
    uv_mutex_unlock(&server->requests.mutex);

    // Wakeup the main event loop to shutdown main thread
    if (server->trace)
    {
        vrtql_trace(VL_INFO, "vrtql_svr_stop(): shutdown main thread");
    }

    uv_async_send(&server->wakeup);

    while (server->state != VS_HALTED)
    {
        sleep(1);
    }
}

//------------------------------------------------------------------------------
// Client connection callbacks
//------------------------------------------------------------------------------

void svr_client_connect(vrtql_svr_cnx* c)
{
    if (c->server->trace)
    {
        vrtql_trace(VL_INFO, "svr_client_connect(%p)", c->client);
    }
}

void svr_client_disconnect(vrtql_svr_cnx* c)
{
    if (c->server->trace)
    {
        vrtql_trace(VL_INFO, "svr_client_disconnect(%p)", c->client);
    }
}

void svr_client_read(vrtql_svr_cnx* c, ssize_t size, const uv_buf_t* buf)
{
    vrtql_svr* server = c->server;

    if (server->trace)
    {
        vrtql_trace(VL_INFO, "svr_client_read(%p)", c);
    }

    // Queue task to worker pool for processing
    vrtql_svr_task* task = vrtql_svr_task_new(c->client, size, buf->base);
    queue_push(&server->requests, task);
}

void svr_client_process(vrtql_svr* server, vrtql_svr_task* req)
{
    //> Prepare the response: echo the data back

    // Allocate memory for the data to be sent in response
    char* data = (char*)vrtql.malloc(req->size);

    // Copy the request's data to the response data
    strncpy(data, req->data, req->size);

    // Create response
    vrtql_svr_task* reply = vrtql_svr_task_new(req->client, req->size, data);

    // Free request
    vrtql_svr_task_free(req);

    if (server->trace)
    {
        vrtql_trace(VL_INFO, "worker_thread(enter) %p queing", reply->client);
    }

    // Send reply. This will wakeup network thread.
    vrtql_svr_send(server, reply);

    if (server->trace)
    {
        vrtql_trace(VL_INFO, "worker_thread(enter) %p done", reply->client);
    }
}

//------------------------------------------------------------------------------
// Server Utilities
//------------------------------------------------------------------------------

vrtql_svr_cnx* svr_cnx_map_get(vrtql_svr_cnx_map* map, uv_stream_t* key)
{
    vrtql_svr_cnx* cnx = sc_map_get_64v(map, (uint64_t)key);

    // If entry exists
    if (sc_map_found(map) == true)
    {
        return cnx;
    }

    return NULL;
}

void svr_cnx_map_remove(vrtql_svr_cnx_map* map, uv_stream_t* key)
{
    vrtql_svr_cnx* cnx = sc_map_get_64v(map, (uint64_t)key);

    // If entry exists
    if (sc_map_found(map) == true)
    {
        sc_map_del_64v(map, (uint64_t)key);

        // Call on_disconnect() handler
        cnx->server->on_disconnect(cnx);

        // Cleanup
        free(cnx);
    }
}

int8_t svr_cnx_map_set( vrtql_svr_cnx_map* map,
                        uv_stream_t* key,
                        vrtql_svr_cnx* value )
{
    // See if we have an existing entry
    sc_map_get_64v(map, (uint64_t)key);

    if (sc_map_found(map) == true)
    {
        // Exsiting entry. Return false.
        return 0;
    }

    sc_map_put_64v(map, (uint64_t)key, value);

    // True
    return 1;
}

void svr_cnx_map_clear(vrtql_svr_cnx_map* map)
{
    uint64_t key; vrtql_svr_cnx* cnx;
    sc_map_foreach(map, key, cnx)
    {
        cnx->server->on_disconnect(cnx);
        free(cnx);
    }

    sc_map_clear_64v(map);
}

vrtql_svr_cnx* svr_cnx_new(vrtql_svr* s, uv_stream_t* c)
{
    vrtql_svr_cnx* cnx = malloc(sizeof(vrtql_svr_cnx));
    cnx->server        = s;
    cnx->client        = c;
    cnx->data          = NULL;

    return cnx;
}

void svr_cnx_free(vrtql_svr_cnx* c)
{
    if (c != NULL)
    {
        free(c);
    }
}

void svr_shutdown(vrtql_svr* server)
{
    queue_destroy(&server->requests);
    queue_destroy(&server->responses);
    uv_stop(server->loop);
}

void svr_on_connect(uv_stream_t* socket, int status)
{
    vrtql_svr* server = (vrtql_svr*) socket->data;

    if (status < 0)
    {
        fprintf( stderr,
                 "Error in connection callback: %s\n",
                 uv_strerror(status) );
        return;
    }

    uv_tcp_t* client = (uv_tcp_t*)malloc(sizeof(uv_tcp_t));

    if (client == NULL)
    {
        fprintf(stderr, "Failed to allocate memory for client\n");
        return;
    }

    client->data = server;

    if (uv_tcp_init(server->loop, client) != 0)
    {
        fprintf(stderr, "Failed to initialize client\n");
        return;
    }

    if (uv_accept(socket, (uv_stream_t*)client) == 0)
    {
        if (uv_read_start((uv_stream_t*)client, svr_on_realloc, svr_on_read) != 0)
        {
            fprintf(stderr, "Failed to start reading from client\n");
            return;
        }
    }
    else
    {
        uv_close((uv_handle_t*)client, svr_on_close);
    }

    //> Add connection to registry and initialize

    vrtql_svr_cnx* cnx = svr_cnx_new(server, (uv_stream_t*)client);

    if (svr_cnx_map_set(&server->cnxs, (uv_stream_t*)client, cnx) == false)
    {
        vrtql.error(VE_FATAL, "Connection already registered");
    }

    //> Call svr_on_connect() handler

    server->on_connect(cnx);
}

void svr_on_read(uv_stream_t* client, ssize_t nread, const uv_buf_t* buf)
{
    vrtql_svr* server = (vrtql_svr*)client->data;

    if (nread < 0)
    {
        uv_close((uv_handle_t*)client, svr_on_close);
        free(buf->base);
        return;
    }

    struct sc_map_64v* map = &server->cnxs;
    vrtql_svr_cnx* cnx     = sc_map_get_64v(map, (uint64_t)client);

    if (sc_map_found(map) == true)
    {
        server->on_read(cnx, nread, buf);
    }
}

void svr_on_write_complete(uv_write_t* req, int status)
{
    vrtql_svr_task_free((vrtql_svr_task*)req->data);
    free(req);
}

void svr_on_close(uv_handle_t* handle)
{
    vrtql_svr* server      = handle->data;
    vrtql_svr_cnx_map* map = &server->cnxs;
    vrtql_svr_cnx* cnx     = sc_map_get_64v(map, (uint64_t)handle);

    // If entry exists
    if (sc_map_found(map) == true)
    {
        // Call on_disconnect() handler
        server->on_disconnect(cnx);

        // Remove from map
        sc_map_del_64v(map, (uint64_t)handle);

        // Cleanup
        free(cnx);
    }

    free(handle);
}

void svr_on_realloc(uv_handle_t* handle, size_t size, uv_buf_t* buf)
{
    buf->base = (char*)malloc(size);
    buf->len  = size;
}

//------------------------------------------------------------------------------
// Queue API
//------------------------------------------------------------------------------

void queue_init(vrtql_svr_queue* queue, int capacity)
{
    queue->buffer   = (vrtql_svr_task**)malloc(capacity * sizeof(vrtql_svr_task*));
    queue->size     = 0;
    queue->capacity = capacity;
    queue->head     = 0;
    queue->tail     = 0;
    queue->state    = VS_RUNNING;
    queue->trace    = 0;

    // Initialize mutex and condition variable
    uv_mutex_init(&queue->mutex);
    uv_cond_init(&queue->cond);
}

void queue_destroy(vrtql_svr_queue* queue)
{
    if (queue->buffer != NULL)
    {
        uv_mutex_destroy(&queue->mutex);
        uv_cond_destroy(&queue->cond);
        free(queue->buffer);
        queue->buffer = NULL;
        queue->state  = VS_HALTED;
    }
}

void queue_push(vrtql_svr_queue* queue, vrtql_svr_task* task)
{
    if (queue->trace)
    {
        vrtql_trace(VL_INFO, "queue_push(enter) %p", task->client);
    }

    uv_mutex_lock(&queue->mutex);

    while (queue->size == queue->capacity)
    {
        uv_cond_wait(&queue->cond, &queue->mutex);
    }

    queue->buffer[queue->tail] = task;
    queue->tail = (queue->tail + 1) % queue->capacity;
    queue->size++;

    // Signal condition variable
    uv_cond_signal(&queue->cond);
    uv_mutex_unlock(&queue->mutex);

    if (queue->trace)
    {
        vrtql_trace(VL_INFO, "queue_push(leave) %p", task->client);
    }
}

vrtql_svr_task* queue_pop(vrtql_svr_queue* queue)
{
    if (queue->trace)
    {
        vrtql_trace(VL_INFO, "queue_pop(enter)");
    }

    uv_mutex_lock(&queue->mutex);

    while (queue->size == 0 && queue->state == VS_RUNNING)
    {
        uv_cond_wait(&queue->cond, &queue->mutex);
    }

    if (queue->trace)
    {
        vrtql_trace(VL_INFO, "queue_pop(wakeup)");
    }

    if (queue->state == VS_HALTING)
    {
        uv_cond_broadcast(&queue->cond);
        uv_mutex_unlock(&queue->mutex);

        return NULL;
    }

    vrtql_svr_task* task = queue->buffer[queue->head];
    queue->head = (queue->head + 1) % queue->capacity;
    queue->size--;
    uv_cond_signal(&queue->cond);
    uv_cond_broadcast(&queue->cond);
    uv_mutex_unlock(&queue->mutex);

    return task;
}

bool queue_empty(vrtql_svr_queue* queue)
{
    uv_mutex_lock(&queue->mutex);
    bool empty = (queue->size == 0);
    uv_mutex_unlock(&queue->mutex);
    return empty;
}

