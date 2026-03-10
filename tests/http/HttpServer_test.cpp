#include <gtest/gtest.h>
#include <vesper/vesper.h>

TEST(HttpServer, CreateObject) {
    vesper::HttpServer server;
    SUCCEED();
}

TEST(HttpServer, SetDomain) {
    vesper::HttpServer server;
    server.domain = "example.com";
    EXPECT_EQ(server.domain, "example.com");
}

TEST(HttpServer, TimeoutValue) {
    vesper::HttpServer server;
    server.timeout = 10;
    EXPECT_EQ(server.timeout, 10);
}

TEST(HttpServer, RegisterGETEndpoint) {
    vesper::HttpServer server;

    server.GET("/test", [](vesper::HttpConnection &conn) { (void)conn; });

    SUCCEED();
}

TEST(HttpServer, RegisterPOSTEndpoint) {
    vesper::HttpServer server;

    server.POST("/submit", [](vesper::HttpConnection &conn) { (void)conn; });

    SUCCEED();
}

TEST(HttpServer, RegisterMultipleHandlers) {
    vesper::HttpServer server;

    server.GET(
        "/chain", [](vesper::HttpConnection &conn) { (void)conn; },
        [](vesper::HttpConnection &conn) { (void)conn; },
        [](vesper::HttpConnection &conn) { (void)conn; });

    SUCCEED();
}

TEST(HttpServer, GlobalMiddleware) {
    vesper::HttpServer server;

    server.use([](vesper::HttpConnection &conn) { (void)conn; });

    SUCCEED();
}

TEST(HttpServer, StaticFileRegistration) {
    vesper::HttpServer server;

    server.staticFile("/logo.png", "assets/logo.png");

    SUCCEED();
}

TEST(HttpServer, StaticDirectoryRegistration) {
    vesper::HttpServer server;

    server.staticDir("/public", "./public");

    SUCCEED();
}

TEST(HttpServer, CreateRouterGroup) {
    vesper::HttpServer server;

    auto router = server.group("/api");

    SUCCEED();
}

TEST(Router, CreateRouterAndAddRoute) {
    vesper::HttpServer server;
    auto router = server.group("/api");

    router.GET("/users", [](vesper::HttpConnection &conn) { (void)conn; });

    SUCCEED();
}

TEST(Router, RouterMiddleware) {
    vesper::HttpServer server;
    auto router = server.group("/api");

    router.use([](vesper::HttpConnection &conn) { (void)conn; });

    router.GET("/users", [](vesper::HttpConnection &conn) { (void)conn; });

    SUCCEED();
}

TEST(Router, MultipleRoutes) {
    vesper::HttpServer server;
    auto router = server.group("/api");

    router.GET("/a", [](vesper::HttpConnection &conn) { (void)conn; });
    router.POST("/b", [](vesper::HttpConnection &conn) { (void)conn; });
    router.PUT("/c", [](vesper::HttpConnection &conn) { (void)conn; });

    SUCCEED();
}