#include "include/server.h"

int main() {
    // Start the server
    http::HttpServer server("localhost", 8080);

    server.setHandler([](http::HttpConnection& conn) {
        conn.sendPlainText(http::HttpResponse::StatusCodes::OK, "Hello World\n");
    });

    server.run();

    return 0;
}