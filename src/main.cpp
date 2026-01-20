#include "include/server.h"

// ====================
// All functions
// ====================
void myHandler(http::HttpConnection& conn);

int main() {
    // Start the server
    http::HttpServer server("localhost", 8080);
    server.setHandler(myHandler);
    server.run();
}

void myHandler(http::HttpConnection& conn) {
    conn.sendPlainText("Hello World\n"); // Default status: 200
}