#include <catch2/catch_all.hpp>
#include <nlohmann/json.hpp>
#include <memory>
#include "MCP/McpProtocol.h"
#include "MCP/ToolSchemas.h"

using json = nlohmann::json;
using namespace Slic3r::GUI;

namespace {

std::unique_ptr<McpProtocol> make_protocol() {
    auto p = std::make_unique<McpProtocol>();
    p->init(get_all_tool_definitions());
    return p;
}

json send_request(McpProtocol& p, const json& req,
                  const std::map<std::string, std::string>& headers = {}) {
    auto resp = p.handle_mcp_request(req.dump(), headers);
    if (resp.body.empty()) return json::object();
    return json::parse(resp.body);
}

std::string do_initialize(McpProtocol& p) {
    json init_req = {
        {"jsonrpc", "2.0"}, {"id", 1}, {"method", "initialize"},
        {"params", {
            {"protocolVersion", "2025-03-26"},
            {"capabilities", json::object()},
            {"clientInfo", {{"name", "test"}, {"version", "1.0"}}}
        }}
    };
    auto resp = p.handle_mcp_request(init_req.dump(), {});
    std::string session_id;
    auto it = resp.extra_headers.find("Mcp-Session-Id");
    if (it != resp.extra_headers.end()) session_id = it->second;

    json notif = {{"jsonrpc", "2.0"}, {"method", "notifications/initialized"}};
    p.handle_mcp_request(notif.dump(), {{"mcp-session-id", session_id}});

    return session_id;
}

} // anonymous namespace

TEST_CASE("MCP protocol lifecycle", "[MCP][Protocol]") {
    auto proto_ptr = make_protocol();
    auto& proto = *proto_ptr;

    SECTION("Initialize returns protocolVersion, capabilities, and serverInfo") {
        json req = {
            {"jsonrpc", "2.0"}, {"id", 1}, {"method", "initialize"},
            {"params", {
                {"protocolVersion", "2025-03-26"},
                {"capabilities", json::object()},
                {"clientInfo", {{"name", "test"}, {"version", "1.0"}}}
            }}
        };
        auto resp = send_request(proto, req);
        REQUIRE(resp.contains("result"));

        auto result = resp["result"];
        REQUIRE(result.contains("protocolVersion"));
        REQUIRE(result.contains("capabilities"));
        REQUIRE(result.contains("serverInfo"));
        REQUIRE(result["protocolVersion"] == MCP_PROTOCOL_VERSION);
        REQUIRE(result["serverInfo"]["name"] == MCP_SERVER_NAME);
        REQUIRE(result["serverInfo"]["version"] == MCP_SERVER_VERSION);
    }

    SECTION("Initialize returns Mcp-Session-Id in extra_headers") {
        json req = {
            {"jsonrpc", "2.0"}, {"id", 1}, {"method", "initialize"},
            {"params", {
                {"protocolVersion", "2025-03-26"},
                {"capabilities", json::object()},
                {"clientInfo", {{"name", "test"}, {"version", "1.0"}}}
            }}
        };
        auto resp = proto.handle_mcp_request(req.dump(), {});
        REQUIRE(resp.extra_headers.count("Mcp-Session-Id") == 1);
        REQUIRE_FALSE(resp.extra_headers.at("Mcp-Session-Id").empty());
    }

    SECTION("tools/list works without prior initialize (lenient local server)") {
        json req = {{"jsonrpc", "2.0"}, {"id", 1}, {"method", "tools/list"}};
        auto resp = send_request(proto, req);
        REQUIRE(resp.contains("result"));
        REQUIRE(resp["result"]["tools"].size() == 21);
    }

    SECTION("notifications/initialized returns 202 status") {
        // First initialize
        json init_req = {
            {"jsonrpc", "2.0"}, {"id", 1}, {"method", "initialize"},
            {"params", {
                {"protocolVersion", "2025-03-26"},
                {"capabilities", json::object()},
                {"clientInfo", {{"name", "test"}, {"version", "1.0"}}}
            }}
        };
        auto init_resp = proto.handle_mcp_request(init_req.dump(), {});
        std::string session_id = init_resp.extra_headers.at("Mcp-Session-Id");

        json notif = {{"jsonrpc", "2.0"}, {"method", "notifications/initialized"}};
        auto resp = proto.handle_mcp_request(notif.dump(), {{"mcp-session-id", session_id}});
        REQUIRE(resp.status_code == 202);
    }

    SECTION("After initialize, tools/list returns tools array") {
        std::string session_id = do_initialize(proto);

        json req = {{"jsonrpc", "2.0"}, {"id", 2}, {"method", "tools/list"}};
        auto resp = send_request(proto, req, {{"mcp-session-id", session_id}});
        REQUIRE(resp.contains("result"));
        REQUIRE(resp["result"].contains("tools"));
        REQUIRE(resp["result"]["tools"].is_array());
        REQUIRE(resp["result"]["tools"].size() > 0);
    }

    SECTION("tools/list returns correct tool count") {
        std::string session_id = do_initialize(proto);

        json req = {{"jsonrpc", "2.0"}, {"id", 2}, {"method", "tools/list"}};
        auto resp = send_request(proto, req, {{"mcp-session-id", session_id}});
        auto tools = resp["result"]["tools"];
        REQUIRE(tools.size() == 21);
    }

    SECTION("tools/call with unknown tool returns error -32602") {
        std::string session_id = do_initialize(proto);

        json req = {
            {"jsonrpc", "2.0"}, {"id", 3}, {"method", "tools/call"},
            {"params", {{"name", "nonexistent_tool"}, {"arguments", json::object()}}}
        };
        auto resp = send_request(proto, req, {{"mcp-session-id", session_id}});
        REQUIRE(resp.contains("error"));
        REQUIRE(resp["error"]["code"] == -32602);
    }

    SECTION("ping returns empty result without session") {
        json req = {{"jsonrpc", "2.0"}, {"id", 1}, {"method", "ping"}};
        auto resp = send_request(proto, req);
        REQUIRE(resp.contains("result"));
        REQUIRE(resp["result"].empty());
    }

    SECTION("Unknown method returns error -32601") {
        std::string session_id = do_initialize(proto);

        json req = {{"jsonrpc", "2.0"}, {"id", 5}, {"method", "unknown/method"}};
        auto resp = send_request(proto, req, {{"mcp-session-id", session_id}});
        REQUIRE(resp.contains("error"));
        REQUIRE(resp["error"]["code"] == -32601);
    }

    SECTION("Stale session ID is accepted (local server is lenient)") {
        json req = {{"jsonrpc", "2.0"}, {"id", 1}, {"method", "tools/list"}};
        auto resp = send_request(proto, req, {{"mcp-session-id", "bogus-session-id"}});
        REQUIRE(resp.contains("result"));
        REQUIRE(resp["result"]["tools"].size() == 21);
    }

    SECTION("No session ID is accepted (local server is lenient)") {
        json req = {{"jsonrpc", "2.0"}, {"id", 1}, {"method", "tools/list"}};
        auto resp = send_request(proto, req, {});
        REQUIRE(resp.contains("result"));
        REQUIRE(resp["result"]["tools"].size() == 21);
    }

    SECTION("Multiple sessions work independently") {
        std::string session1 = do_initialize(proto);
        std::string session2 = do_initialize(proto);

        REQUIRE(session1 != session2);

        // Both sessions can list tools
        json req = {{"jsonrpc", "2.0"}, {"id", 1}, {"method", "tools/list"}};
        auto resp1 = send_request(proto, req, {{"mcp-session-id", session1}});
        auto resp2 = send_request(proto, req, {{"mcp-session-id", session2}});

        REQUIRE(resp1.contains("result"));
        REQUIRE(resp2.contains("result"));
        REQUIRE(resp1["result"]["tools"].size() == resp2["result"]["tools"].size());
    }
}
