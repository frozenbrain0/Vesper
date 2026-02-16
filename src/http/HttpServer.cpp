#include "../include/http/HttpServer.h"
#include "../include/http/radixTree.h"
#include "../include/tcp/TcpServer.h"

namespace vesper {
// ===========
// HTTP-SERVER
// ===========

HttpServer::HttpServer() : TcpServer() {}

// Close server automatically
HttpServer::~HttpServer() {
    if (serverThread.joinable()) {
        serverThread.join();
    }
}

// Sets up & runs the server using the previously created objects
void HttpServer::run(std::string ipAddress, int port) {
    setupLogger();
    if (ipAddress == "localhost")
        ipAddress = "127.0.0.1";
    startServer(ipAddress, port);
    serverThread = std::thread(&TcpServer::runServer, this);
}

// Decides when & at what endpoint to run the handlers
void HttpServer::onClient(int client) {
    // Create the object that gets access by the library user
    HttpConnection connection(client, this);
    Context ctx{};
    ctx.buffer.resize(4096);

    // Getting what endpoint, method client has/wants to later run the correct
    // handler Receive data from client;
    if (!receiveClientRequest(client, ctx)) {
        // Specific logs already handled
        close(client);
        return;
    }
    // Parse http data
    if (!parseRequestLine(ctx)) {
        log(LogType::Warn, "Failed to parse http request");
        close(client);
        return;
    }
    // Skip Website Logo if client connected with a browser
    if (shouldCloseConnection(ctx)) {
        // No logs because would be annoying to log this
        close(client);
        return;
    }
    // Parse remaining headers
    connection.request.headers =
        parseHeaders(ctx.request.data(), ctx.request.size());
    // Get the content length
    getContentLength(ctx);
    // Find end of headers
    parseHeadersAndBody(ctx);
    // Save the body to postData
    if (!fetchPostData(client, ctx)) {
        // Specific error is already logged
        close(client);
        return;
    }
    // Saves all the variables to connection
    populateConnection(connection, ctx);
    // Adjust clientEndpoint given to the handlers so querys are
    // disregarded
    getQuery(ctx, connection);
    // MiddleWare / All Handlers
    parseUrlParameters(ctx, connection);
    // Run Middlewares and handler
    handleRequest(connection, ctx);
    // Close connection and logs
    finalizeRequest(connection, client);
}

// Getting what endpoint, method client has/wants to later run the correct
// handler Receive data from client;
bool HttpServer::receiveClientRequest(int client, Context &ctx) {
    return receiveRequest(client, ctx.request, ctx.buffer);
}

// https://cplusplus.com/reference/cstdio/sscanf/
bool HttpServer::parseRequestLine(Context &ctx) {
    return sscanf(ctx.request.data(), "%s %s %s", ctx.method,
                  ctx.clientEndpoint, ctx.version) == 3;
}

// Skip Website Logo if client connected with a browser
bool HttpServer::shouldCloseConnection(Context &ctx) {
    return strcmp(ctx.clientEndpoint, "/favicon.ico") == 0;
}

void HttpServer::getContentLength(Context &ctx) {
    ctx.contentLength = 0;
    char *cl = strstr(ctx.request.data(), "Content-Length:");
    if (cl) {
        sscanf(cl, "Content-Length: %d", &ctx.contentLength);
    }
}

// Find end of headers
void HttpServer::parseHeadersAndBody(Context &ctx) {
    ctx.headerEnd = ctx.request.find("\r\n\r\n");
    if (ctx.headerEnd != std::string::npos &&
        ctx.headerEnd + 4 < ctx.request.size()) {
        ctx.body = ctx.request.substr(ctx.headerEnd + 4);
    }
}

// Save the body to postData
bool HttpServer::fetchPostData(int client, Context &ctx) {
    ctx.postData = ctx.body;
    if (!receivePostData(client, ctx.buffer, ctx.postData, timeout,
                         ctx.contentLength)) {
        close(client);
        return false;
    }
    return true;
}

void HttpServer::populateConnection(HttpConnection &connection, Context &ctx) {
    connection.setClientBuffer(ctx.postData);
    connection.request.method = std::string(ctx.method);
    connection.request.path = ctx.clientEndpoint;
    connection.request.httpVersion = ctx.version;
}

// Adjust clientEndpoint given to the handlers so querys are
// disregarded
void HttpServer::getQuery(Context &ctx, HttpConnection &connection) {
    ctx.endpointStr = std::string(ctx.clientEndpoint);
    auto pos = ctx.endpointStr.find('?');
    if (pos != std::string::npos) {
        std::string path = ctx.endpointStr.substr(0, pos);
        std::string query = ctx.endpointStr.substr(pos + 1);

        std::snprintf(ctx.clientEndpoint, sizeof(ctx.clientEndpoint), "%s",
                      path.c_str());
        ctx.endpointStr = path;
        connection.request.rawQuery = decodeURL(query);
    }
}

void HttpServer::parseUrlParameters(Context &ctx, HttpConnection &connection) {
    std::unordered_map<std::string, std::string> parameterMap =
        endpointsTree.getUrlParams(ctx.endpointStr, std::string(ctx.method));
    for (auto &pair : parameterMap) {
        pair.second = decodeURL(pair.second);
    }
    connection.request.params = parameterMap;
}

void HttpServer::handleRequest(HttpConnection &connection, Context &ctx) {

    std::vector<std::function<void(HttpConnection &)>> middlewares;
    middlewareTree.collectPrefixHandlers(ctx.endpointStr, ctx.method,
                                         middlewares);

    bool handled = false;

    runMiddlewareChain(connection, middlewares, 0, [&]() {
        if (endpointsTree.matchURL(ctx.endpointStr, ctx.method)) {
            auto h = endpointsTree.getNodeHandler(ctx.endpointStr, ctx.method);
            if (h) {
                h(connection);
                handled = true;
            }
        }
    });

    if (!handled)
        connection.string(404, "");
}

void HttpServer::finalizeRequest(HttpConnection &connection, int client) {
    connection.sendBuffer();
    close(client);
    logConnection(static_cast<int>(connection.response.status),
                  connection.response.method, connection.request.path);
}

vesper::Router HttpServer::group(std::string endpoint) {
    return vesper::Router(*this, endpoint);
}
// Endpoint
// Create & Save Endpoint to allEndpoints so it is handled in
// onClient()
void HttpServer::createEndpoint(std::string method, std::string endpoint,
                                std::function<void(HttpConnection &)> h) {
    endpointsTree.addURL(endpoint, method, false, h);
    log(LogType::Info, method + " " + endpoint);
}

// Middleware
// Create a middleware by saving it to allMiddleware
// This then gets processed in runMiddlewares()
void HttpServer::setMiddleware(std::string endpoint, std::string method,
                               bool prefix,
                               std::function<void(HttpConnection &)> handler) {
    middlewareTree.addURL(endpoint, method, prefix, handler);
    log(LogType::Info, method + " " + endpoint);
}

// Recursive function that goes through every Middleware to decide
// what to run
void HttpServer::runMiddlewareChain(
    HttpConnection &c, std::vector<std::function<void(HttpConnection &)>> &mws,
    size_t index, std::function<void()> finalHandler) {
    if (index >= mws.size()) {
        finalHandler();
        return;
    }

    bool nextCalled = false;

    c.setNext([&]() {
        nextCalled = true;
        runMiddlewareChain(c, mws, index + 1, finalHandler);
    });

    mws[index](c);

    // Stop chain if middleware didn't call next()
    if (!nextCalled) {
        return;
    }
}

// Header Parsing
std::unordered_map<std::string, std::string>
HttpServer::parseHeaders(const char *buffer, int headerSize) {
    std::unordered_map<std::string, std::string> receivedHeaders;
    std::string headerStr(buffer, headerSize);
    // https://stackoverflow.com/questions/12514510/iterate-through-lines-in-a-string-c
    std::istringstream stream(headerStr);
    std::string line;

    while (std::getline(stream, line)) {
        // Remove new line character at the end of the line
        if (!line.empty() && line.back() == '\r')
            line.pop_back();

        if (line.empty())
            break;

        // Split key and value
        int colon = line.find(':');
        if (colon == std::string::npos)
            continue;

        std::string key = line.substr(0, colon);
        std::string value = line.substr(colon + 1);

        // Remove white spaces
        // https://stackoverflow.com/questions/83439/remove-spaces-from-stdstring-in-c
        key.erase(std::remove_if(key.begin(), key.end(), ::isspace), key.end());
        // Erase from 0 to the first non space/tab character
        value.erase(0, value.find_first_not_of(" \t"));

        receivedHeaders[key] = value;
    }

    return receivedHeaders;
}
} // namespace vesper

namespace vesper {
// Responsible for server.group()
// Works by reusing HttpServer() functions just with the correct prefix
Router::Router(HttpServer &server, std::string prefix)
    : server(server), prefix(prefix) {}

std::string Router::validatePath(std::string endpoint) {
    std::string path = endpoint;
    // ensure leading slash
    if (path[0] != '/')
        path = '/' + path;

    if (!prefix.empty()) {
        std::string fullPath = prefix;
        if (fullPath.back() == '/')
            fullPath.pop_back(); // remove trailing slash
        path = fullPath + path;
    }
    return path;
}
} // namespace vesper