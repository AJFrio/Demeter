#pragma once

#include "control.h"

#include <string>

namespace demeter {

struct ApiRequest {
    std::string method;
    std::string path;
    std::string body;
    std::string token;
    bool bypassAuth = false;
};

struct ApiResponse {
    int status = 200;
    std::string body;
};

ApiResponse dispatch(Controller& controller, const ApiRequest& request);
bool parseSerialLine(const char* line, ApiRequest& request, std::string& error);

std::string exportPersisted(const Controller& controller);
bool importPersisted(Controller& controller, const char* json);
extern const char* kApiHelp;

}  // namespace demeter
