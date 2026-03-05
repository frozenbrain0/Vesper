#include <vesper/vesper.h>

// ====================
// All functions
// ====================
void myHandler(vesper::HttpConnection &c);
void testEndpoint(vesper::HttpConnection &c);

int main() {
    // Start the server
    vesper::HttpServer server;

    // Route handlers
    server.GET("/", myHandler);        // Hello World endpoint
    server.GET("/test", testEndpoint); // JSON endpoint

    server.run("localhost", 8080);
}

void myHandler(vesper::HttpConnection &c) { c.string("Hello World"); }

struct myJson {
    std::string status = "OK";
    std::string message = "This is a test message";
};
void testEndpoint(vesper::HttpConnection &c) {
    myJson t;
    std::string json = c.shouldBindJson(t);
    c.json(json);
}