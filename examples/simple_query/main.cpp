// Example: authenticate and list computers via veyon32api v1
//
// Usage: example-simple-query <host> <port> <keyName> <keyFile>

#include <httplib.h>
#include <nlohmann/json.hpp>
#include <iostream>

int main(int argc, char* argv[])
{
    const char* host    = (argc > 1) ? argv[1] : "127.0.0.1";
    int         port    = (argc > 2) ? std::stoi(argv[2]) : 11081;

    httplib::Client cli(host, port);

    // 1. Authenticate (placeholder until auth is implemented)
    auto authRes = cli.Post("/api/v1/auth",
        R"({"method":"veyon-key","keyName":"default","keyData":""})",
        "application/json");

    if (!authRes || authRes->status != 200) {
        std::cerr << "Auth failed\n";
        return 1;
    }

    auto j = nlohmann::json::parse(authRes->body);
    std::string token = j.value("token", "");
    std::cout << "Token: " << token << "\n";

    // 2. List computers
    httplib::Headers headers{ {"Authorization", "Bearer " + token} };
    auto listRes = cli.Get("/api/v1/computers", headers);

    if (listRes && listRes->status == 200) {
        std::cout << nlohmann::json::parse(listRes->body).dump(2) << "\n";
    }

    return 0;
}
