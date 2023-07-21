#include "server.h"
#include "message.h"

#include "common.h"

cstr server_host = "127.0.0.1";
int  server_port = 8181;
cstr uri         = "ws://localhost:8181/websocket";
cstr content     = "Lorem ipsum dolor sit amet";

// Server function to process messages. Runs in context of worker thread.
void process_message(vrtql_svr_cnx* cnx, vrtql_msg* m)
{
    vrtql_msg_svr* server = (vrtql_msg_svr*)cnx->server;

    vrtql.trace(VL_INFO, "process_message (%p) %p", cnx, m);

    // Echo back. Note: You should always set reply messages format to the
    // format of the connection.

    // Create reply message
    vrtql_msg* reply = vrtql_msg_new();
    reply->format    = cnx->format;

    // Copy content
    cstr data   = m->content->data;
    size_t size = m->content->size;
    vrtql_buffer_append(reply->content, data, size);

    // Send. We don't free message as send() does it for us.
    server->send(cnx, reply);

    // Clean up request
    vrtql_msg_free(m);
}

void server_thread(void* arg)
{
    vrtql_svr* server = (vrtql_svr*)arg;
    vrtql.tracelevel  = VT_THREAD;
    server->trace     = vrtql.tracelevel;

    vrtql_svr_run(server, server_host, server_port);
}

void client_thread(void* arg)
{
    vws_cnx* cnx = vws_cnx_new();
    ASSERT_TRUE(vws_connect(cnx, uri));

    cstr payload = "payload";

    int i = 0;
    while (true)
    {
        if (i++ > 10)
        {
            break;
        }

        // Create
        vrtql_msg* request = vrtql_msg_new();
        vrtql_msg_set_content(request, payload);

        // Send
        ASSERT_TRUE(vrtql_msg_send(cnx, request) > 0);

        // Receive
        vrtql_msg* reply = vrtql_msg_recv(cnx);

        // Check
        cstr content = reply->content->data;
        size_t size  = reply->content->size;
        ASSERT_TRUE(strncmp(payload, content, size) == 0);

        // Cleanup
        vrtql_msg_free(request);
        vrtql_msg_free(reply);
    }

    // Disconnect
    vws_disconnect(cnx);
    vws_cnx_free(cnx);
}

CTEST(test_msg_server, echo)
{
    vrtql_msg_svr* server = vrtql_msg_svr_new(10, 0, 0);
    server->process       = process_message;

    uv_thread_t server_tid;
    uv_thread_create(&server_tid, server_thread, server);

    // Wait for server to start up
    while (vrtql_svr_state((vrtql_svr*)server) != VS_RUNNING)
    {
        vrtql_msleep(100);
    }

    int nc = 10;
    uv_thread_t* threads = vrtql.malloc(sizeof(uv_thread_t) * nc);

    for (int i = 0; i < nc; i++)
    {
        uv_thread_create(&threads[i], client_thread, NULL);
        vrtql.trace(VL_INFO, "started client thread %p", threads[i]);
    }

    for (int i = 0; i < nc; i++)
    {
        uv_thread_join(&threads[i]);
        vrtql.trace(VL_INFO, "stopped client thread %p", threads[i]);
    }

    free(threads);

    // Shutdown
    vrtql_svr_stop((vrtql_svr*)server);
    uv_thread_join(&server_tid);
    vrtql_msg_svr_free(server);
}

int main(int argc, const char* argv[])
{
    return ctest_main(argc, argv);
}
