// Example: lock all computers in a classroom via batch API (v2)
//
// Usage: example-batch-control <host> <port> <token> <locationId>

#include <httplib.h>
#include <nlohmann/json.hpp>
#include <iostream>
#include <vector>

int main(int argc, char* argv[])
{
    const char* host       = (argc > 1) ? argv[1] : "127.0.0.1";
    int         port       = (argc > 2) ? std::stoi(argv[2]) : 11081;
    const char* token      = (argc > 3) ? argv[3] : "";
    const char* locationId = (argc > 4) ? argv[4] : "";

    httplib::Client cli(host, port);
    httplib::Headers headers{ {"Authorization", std::string("Bearer ") + token} };

    // 1. Get computers in location
    auto locRes = cli.Get(std::string("/api/v2/locations/") + locationId, headers);
    if (!locRes || locRes->status != 200) {
        std::cerr << "Failed to get location\n";
        return 1;
    }

    auto loc = nlohmann::json::parse(locRes->body);
    std::vector<std::string> ids = loc.value("computerIds", std::vector<std::string>{});

    // 2. Lock all computers via batch endpoint
    nlohmann::json req;
    req["computerIds"] = ids;
    req["featureUid"]  = "ccb535a2-1d24-4cc1-a709-8b47d2b2ac79"; // screen lock UID
    req["operation"]   = "start";
    req["arguments"]   = nlohmann::json::object();

    auto batchRes = cli.Post("/api/v2/batch/features", headers, req.dump(), "application/json");
    if (batchRes) {
        std::cout << nlohmann::json::parse(batchRes->body).dump(2) << "\n";
    }

    return 0;
}
