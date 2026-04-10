#pragma once
#include "Arduino.h"
#include <functional>
#include <iostream>
#include <map>
#include <string>
#include <vector>

enum HTTPMethod {
  HTTP_ANY,
  HTTP_GET,
  HTTP_POST,
  HTTP_PUT,
  HTTP_PATCH,
  HTTP_DELETE,
  HTTP_OPTIONS
};

class WebServer {
public:
  int port;
  // Handler map now needs to store method too? For simplicity, we ignore method
  // in mock or store in key? Key = "METHOD:URI" ?
  std::map<String, std::function<void()>> handlers;

  // Simulate Request Data
  std::map<String, String> currentArgs;
  HTTPMethod currentMethod = HTTP_GET;

  WebServer(int p) : port(p) {}

  void on(const char *uri, std::function<void()> handler) {
    handlers[String(uri)] = handler;
  }

  // Overload for method specific handling
  void on(const char *uri, HTTPMethod method, std::function<void()> handler) {
    // For mock simplicity, just overwrite generic
    handlers[String(uri)] = handler;
  }

  void onNotFound(std::function<void()> handler) { handlers["404"] = handler; }

  void begin() { std::cout << "[WebServer] Started on port " << port << "\n"; }

  void handleClient() {}

  void send(int code, const char *content_type, String content) {
    std::cout << "[WebServer] SEND " << code << " (" << content_type << ")\n";
  }

  // Send PROGMEM string
  void send_P(int code, const char *content_type, const char *content) {
    send(code, content_type, String(content)); // Cast to string for mock
  }

  bool hasArg(const char *name) { return currentArgs.count(name); }

  String arg(const char *name) {
    if (currentArgs.count(name))
      return currentArgs[name];
    return "";
  }

  // Helper to simulate receiving a POST/GET
  // Expanded to support multiple args or JSON body ("plain")
  void mockRequest(String p_argName, String p_argValue, String uri) {
    // Legacy helper support
    currentArgs.clear();
    currentArgs[p_argName] = p_argValue;
    if (handlers.count(uri))
      handlers[uri]();
    else
      std::cout << "[WebServer] 404: " << uri << "\n";
  }

  // Newer mock helper for API testing
  void mockApiRequest(String uri, String body = "") {
    currentArgs.clear();
    if (body.length() > 0)
      currentArgs["plain"] = body;

    if (handlers.count(uri)) {
      // std::cout << "[WebServer] Mock API: " << uri << "\n";
      handlers[uri]();
    } else {
      std::cout << "[WebServer] 404: " << uri << "\n";
    }
  }
};
