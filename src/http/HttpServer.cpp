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
    log(LogType::Info, "Starting Server");
    if (ipAddress == "localhost")
        ipAddress = "127.0.0.1";
    startServer(ipAddress, port);
    serverThread = std::thread(&TcpServer::runServer, this);
}

// Decides when & at what endpoint to run the handlers
void HttpServer::onClient(int client) {
    log(LogType::Debug, "New Client");
    log(LogType::Debug, "Create HttpConnection object");
    // Create the object that gets access by the library user
    HttpConnection connection(client, this);

    log(LogType::Debug, "Use fcntl to make recv non blocking");
    int flags = fcntl(client, F_GETFL, 0);
    if (flags == -1) {
        perror("fcntl F_GETFL");
        close(client);
        return;
    }

    if (fcntl(client, F_SETFL, flags | O_NONBLOCK) == -1) {
        perror("fcntl F_SETFL");
        close(client);
        return;
    }
    // Getting what endpoint, method client has/wants to later run the correct
    // handler Receive data from client;
    log(LogType::Debug, "Read client request");

    std::string request;
    std::vector<char> buffer(4096);
    while (request.find("\r\n\r\n") == std::string::npos) {
        int r = recv(client, buffer.data(), buffer.size(), 0);
        if (r > 0) {
            request.append(buffer.data(), r);
        } else if (r == 0) {
            log(LogType::Warn, "Client closed connection");
            close(client);
            return;
        } else { // r < 0
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                // no data yet -> wait a little, then retry
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
                continue;
            } else {
                log(LogType::Warn, "recv error");
                close(client);
                return;
            }
        }

        // Prevent header abuse
        if (request.size() > 16 * 1024) {
            log(LogType::Warn, "HTTP headers too large");
            close(client);
            return;
        }
    }

    // Parse http data
    log(LogType::Debug, "Parse method, endpoint, version");
    char method[10], clientEndpoint[256], version[10];
    if (sscanf(request.data(), "%s %s %s", method, clientEndpoint, version) !=
        3) { // https://cplusplus.com/reference/cstdio/sscanf/
        log(LogType::Warn, "Failed to parse http request");
    }

    // Skip Website Logo if client connected with a browser
    log(LogType::Debug, "Skip favicon");
    if (strcmp(clientEndpoint, "/favicon.ico") == 0) {
        log(LogType::Debug, "Ignoring favicon request");
        close(client);
        return;
    }

    // Parse remaining headers
    log(LogType::Debug, "Parse remaining headers");
    connection.request.headers = parseHeaders(request.data(), request.size());

    // Get the content length
    log(LogType::Debug, "Get content length");
    int contentLength = 0;
    char *cl = strstr(request.data(), "Content-Length:");
    if (cl) {
        sscanf(cl, "Content-Length: %d", &contentLength);
    }

    // Find end of headers
    log(LogType::Debug, "Find end of headers");
    std::string header = "";
    int headerEnd = request.find("\r\n\r\n");
    std::string headers = request.substr(0, headerEnd);
    std::string body;
    if (headerEnd + 4 < request.size()) {
        body = request.substr(headerEnd + 4);
    }

    // Save the body to postData
    log(LogType::Debug, "Save postdata");
    std::string postData = body;
    auto start = std::chrono::steady_clock::now();
    const int maxTotalSeconds = 2;
    while (postData.size() < contentLength) {
        int r = recv(client, buffer.data(), buffer.size(), 0);
        if (r > 0) {
            postData.append(buffer.data(), r);
        } else if (r == 0) {
            log(LogType::Warn, "Client closed during body");
            close(client);
            return;
        } else {
            if (errno == EAGAIN ||
                errno == EWOULDBLOCK) { // Not a timeout — just no data yet
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            } else {
                log(LogType::Warn, "recv error");
                close(client);
                return;
            }
        }

        auto now = std::chrono::steady_clock::now();
        if (std::chrono::duration_cast<std::chrono::seconds>(now - start)
                .count() > maxTotalSeconds) {
            log(LogType::Warn, "Client took too long to send body");
            close(client);
            return;
        }
    }

    log(LogType::Debug, "Save body to request.body");
    connection.setClientBuffer(postData);
    log(LogType::Debug, "Save headers");
    connection.request.rawHeaders = header;
    log(LogType::Debug, "Save method");
    connection.request.method = std::string(method);
    log(LogType::Debug, "Save endpoint");
    connection.request.path = clientEndpoint;
    log(LogType::Debug, "Save version");
    connection.request.httpVersion = version;
    bool handled = false;

    // Adjust clientEndpoint given to the handlers so querys are
    // disregarded
    log(LogType::Debug, "Find ? for querys");
    std::string endpointStr(clientEndpoint);
    auto pos = endpointStr.find('?');
    if (pos != std::string::npos) {
        log(LogType::Debug, "Found query");
        // First get position to avoid std::out_of_range
        std::string path = endpointStr.substr(0, pos);
        std::string query = endpointStr.substr(pos + 1);

        std::snprintf(clientEndpoint, sizeof(clientEndpoint), "%s",
                      path.c_str());
        endpointStr = path;
        log(LogType::Debug, "Save query");
        connection.request.rawQuery = decodeURL(query);
    }

    log(LogType::Debug, "Get URL params");
    std::unordered_map<std::string, std::string> parameterMap =
        endpointsTree.getUrlParams(endpointStr, std::string(method));
    for (auto &pair : parameterMap) {
        log(LogType::Debug, "Save parameter to map");
        pair.second = decodeURL(pair.second);
    }
    log(LogType::Debug, "Save parameters map");
    connection.request.params = parameterMap;

    // MiddleWare / All Handlers
    log(LogType::Debug, "Run Middleware Chain");
    std::vector<std::function<void(HttpConnection &)>> middlewares;
    middlewareTree.collectPrefixHandlers(endpointStr, method, middlewares);

    runMiddlewareChain(connection, middlewares, 0, [&]() {
        if (endpointsTree.matchURL(endpointStr, method)) {
            log(LogType::Debug, "Run endpoint handler");
            auto h = endpointsTree.getNodeHandler(endpointStr, method);
            if (h) {
                h(connection);
                handled = true;
            }
        }
    });

    if (!handled)
        connection.string(404, "");

    log(LogType::Debug, "Send buffer");
    connection.sendBuffer();
    log(LogType::Debug, "Close client connection");
    close(client);

    // On the bottom so favicon.ico is skipped in the logs
    log(LogType::Debug, "Client accepted");
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
    log(LogType::Info, "Succesfully created endpoint [" + endpoint + "]");
}

// Middleware
// Create a middleware by saving it to allMiddleware
// This then gets processed in runMiddlewares()
void HttpServer::setMiddleware(std::string endpoint, std::string method,
                               bool prefix,
                               std::function<void(HttpConnection &)> handler) {
    middlewareTree.addURL(endpoint, method, prefix, handler);
    log(LogType::Info, "Succesfully created middleware  [" + endpoint + "]");
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