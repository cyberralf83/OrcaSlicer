#include <catch2/catch_all.hpp>
#include <nlohmann/json.hpp>
#include "MCP/ToolSchemas.h"

#include <set>

using json = nlohmann::json;
using namespace Slic3r::GUI;

TEST_CASE("Tool schema definitions", "[MCP][ToolSchemas]") {
    auto tools = get_all_tool_definitions();

    SECTION("get_all_tool_definitions returns 21 tools") {
        REQUIRE(tools.size() == 21);
    }

    SECTION("Each tool has non-empty name and description") {
        for (const auto& tool : tools) {
            INFO("Tool: " + tool.name);
            REQUIRE_FALSE(tool.name.empty());
            REQUIRE_FALSE(tool.description.empty());
        }
    }

    SECTION("Each inputSchema has type=object") {
        for (const auto& tool : tools) {
            INFO("Tool: " + tool.name);
            REQUIRE(tool.input_schema.contains("type"));
            REQUIRE(tool.input_schema["type"] == "object");
        }
    }

    SECTION("No duplicate tool names") {
        std::set<std::string> names;
        for (const auto& tool : tools) {
            INFO("Duplicate tool name: " + tool.name);
            REQUIRE(names.insert(tool.name).second);
        }
    }

    SECTION("Screenshot tool exists with is_screenshot=true") {
        const McpToolDef* screenshot = nullptr;
        for (const auto& tool : tools) {
            if (tool.name == "screenshot") {
                screenshot = &tool;
                break;
            }
        }
        REQUIRE(screenshot != nullptr);
        REQUIRE(screenshot->is_screenshot);
    }

    SECTION("Screenshot tool has width, height, mode properties") {
        const McpToolDef* screenshot = nullptr;
        for (const auto& tool : tools) {
            if (tool.name == "screenshot") {
                screenshot = &tool;
                break;
            }
        }
        REQUIRE(screenshot != nullptr);

        auto props = screenshot->input_schema["properties"];
        REQUIRE(props.contains("width"));
        REQUIRE(props.contains("height"));
        REQUIRE(props.contains("mode"));
    }

    SECTION("model_add_primitive has required field with type") {
        const McpToolDef* tool = nullptr;
        for (const auto& t : tools) {
            if (t.name == "model_add_primitive") {
                tool = &t;
                break;
            }
        }
        REQUIRE(tool != nullptr);
        REQUIRE(tool->input_schema.contains("required"));

        auto required = tool->input_schema["required"];
        REQUIRE(required.is_array());
        bool has_type = false;
        for (const auto& r : required) {
            if (r == "type") has_type = true;
        }
        REQUIRE(has_type);
    }

    SECTION("config_get has required field with keys") {
        const McpToolDef* tool = nullptr;
        for (const auto& t : tools) {
            if (t.name == "config_get") {
                tool = &t;
                break;
            }
        }
        REQUIRE(tool != nullptr);
        REQUIRE(tool->input_schema.contains("required"));

        auto required = tool->input_schema["required"];
        REQUIRE(required.is_array());
        bool has_keys = false;
        for (const auto& r : required) {
            if (r == "keys") has_keys = true;
        }
        REQUIRE(has_keys);
    }
}
