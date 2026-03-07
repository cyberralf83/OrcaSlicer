#include "MCP/ToolSchemas.h"

using json = nlohmann::json;

namespace Slic3r { namespace GUI {

std::vector<McpToolDef> get_all_tool_definitions()
{
    std::vector<McpToolDef> tools;

    // -----------------------------------------------------------------------
    // screenshot (special handler, not dispatched via CommandDispatch)
    // -----------------------------------------------------------------------
    tools.push_back({
        "screenshot",
        "Capture the current OrcaSlicer viewport as a PNG image. By default captures exactly what the user sees (viewport mode). Use mode=thumbnail for a fixed orthographic thumbnail.",
        {
            {"type", "object"},
            {"properties", {
                {"width",  {{"type", "integer"}, {"description", "Image width in pixels (only used in thumbnail mode)"}}},
                {"height", {{"type", "integer"}, {"description", "Image height in pixels (only used in thumbnail mode)"}}},
                {"mode",   {{"type", "string"}, {"enum", json::array({"viewport", "thumbnail"})}, {"description", "Capture mode: 'viewport' (default) captures the actual screen, 'thumbnail' uses FBO orthographic rendering"}}}
            }}
        },
        "", // no dispatch action
        true // is_screenshot
    });

    // -----------------------------------------------------------------------
    // Model tools
    // -----------------------------------------------------------------------
    tools.push_back({
        "model_list_objects",
        "List all objects currently on the build plate.",
        {
            {"type", "object"},
            {"properties", json::object()}
        },
        "model.list"
    });

    tools.push_back({
        "model_add_primitive",
        "Add a primitive shape (cube, cylinder, or sphere) to the build plate.",
        {
            {"type", "object"},
            {"properties", {
                {"type", {{"type", "string"}, {"enum", json::array({"cube", "cylinder", "sphere"})}, {"description", "Primitive shape type"}}},
                {"x",    {{"type", "number"}, {"description", "X dimension/size"}}},
                {"y",    {{"type", "number"}, {"description", "Y dimension/size"}}},
                {"z",    {{"type", "number"}, {"description", "Z dimension/size"}}}
            }},
            {"required", json::array({"type"})}
        },
        "model.add_primitive"
    });

    tools.push_back({
        "model_load_file",
        "Load a 3D model file (STL, 3MF, OBJ, STEP) onto the build plate.",
        {
            {"type", "object"},
            {"properties", {
                {"path", {{"type", "string"}, {"description", "Absolute path to the model file"}}}
            }},
            {"required", json::array({"path"})}
        },
        "model.load_file"
    });

    tools.push_back({
        "model_delete_object",
        "Delete an object from the build plate by its index.",
        {
            {"type", "object"},
            {"properties", {
                {"index", {{"type", "integer"}, {"description", "Index of the object to delete"}}}
            }},
            {"required", json::array({"index"})}
        },
        "model.delete"
    });

    tools.push_back({
        "object_transform",
        "Transform an object on the build plate: translate, scale, and/or rotate.",
        {
            {"type", "object"},
            {"properties", {
                {"index",     {{"type", "integer"}, {"description", "Index of the object to transform"}}},
                {"translate", {{"type", "array"}, {"items", {{"type", "number"}}}, {"minItems", 3}, {"maxItems", 3}, {"description", "Translation [x, y, z] in mm"}}},
                {"scale",     {{"type", "array"}, {"items", {{"type", "number"}}}, {"minItems", 3}, {"maxItems", 3}, {"description", "Scale factors [x, y, z]"}}},
                {"rotate",    {{"type", "array"}, {"items", {{"type", "number"}}}, {"minItems", 3}, {"maxItems", 3}, {"description", "Rotation angles [x, y, z] in degrees"}}}
            }},
            {"required", json::array({"index"})}
        },
        "model.transform"
    });

    tools.push_back({
        "model_export",
        "Export the current model/plate to a file.",
        {
            {"type", "object"},
            {"properties", {
                {"path",   {{"type", "string"}, {"description", "Absolute path for the exported file"}}},
                {"format", {{"type", "string"}, {"enum", json::array({"3mf", "stl"})}, {"description", "Export format (default: 3mf)"}}}
            }},
            {"required", json::array({"path"})}
        },
        "model.export"
    });

    // -----------------------------------------------------------------------
    // Config tools
    // -----------------------------------------------------------------------
    tools.push_back({
        "config_get",
        "Get the current values of one or more OrcaSlicer config settings.",
        {
            {"type", "object"},
            {"properties", {
                {"keys", {{"type", "array"}, {"items", {{"type", "string"}}}, {"description", "List of config keys to retrieve"}}}
            }},
            {"required", json::array({"keys"})}
        },
        "config.get"
    });

    tools.push_back({
        "config_set",
        "Set one or more OrcaSlicer config settings. Pass key-value pairs.",
        {
            {"type", "object"},
            {"properties", {
                {"settings", {{"type", "object"}, {"additionalProperties", true}, {"description", "Key-value pairs of config settings to update"}}}
            }},
            {"required", json::array({"settings"})}
        },
        "config.set"
    });

    tools.push_back({
        "config_list_options",
        "List available config options, optionally filtered by a search term.",
        {
            {"type", "object"},
            {"properties", {
                {"filter", {{"type", "string"}, {"description", "Filter string to narrow results"}}}
            }}
        },
        "config.list"
    });

    tools.push_back({
        "config_load_profile",
        "Load a printer/material/process profile by name or file path.",
        {
            {"type", "object"},
            {"properties", {
                {"name", {{"type", "string"}, {"description", "Profile name to load"}}},
                {"path", {{"type", "string"}, {"description", "File path to a profile"}}}
            }}
        },
        "config.load_profile"
    });

    // -----------------------------------------------------------------------
    // Diagnostics tools
    // -----------------------------------------------------------------------
    tools.push_back({
        "mesh_stats",
        "Get mesh statistics for an object (vertex count, face count, volume, etc.).",
        {
            {"type", "object"},
            {"properties", {
                {"index", {{"type", "integer"}, {"description", "Index of the object"}}}
            }},
            {"required", json::array({"index"})}
        },
        "diagnostics.mesh_stats"
    });

    tools.push_back({
        "validate_print",
        "Validate the current print setup and report any warnings or errors.",
        {
            {"type", "object"},
            {"properties", json::object()}
        },
        "diagnostics.validate"
    });

    tools.push_back({
        "slice_and_stats",
        "Slice the current plate and return slicing statistics (time, filament usage, layers, etc.).",
        {
            {"type", "object"},
            {"properties", json::object()}
        },
        "diagnostics.slice"
    });

    tools.push_back({
        "slice_status",
        "Check the current slicing status. Returns whether slicing is in progress, finished, print time estimates, filament usage, and any validation errors/warnings.",
        {
            {"type", "object"},
            {"properties", json::object()}
        },
        "diagnostics.slice_status"
    });

    tools.push_back({
        "get_state",
        "Get the current overall state of OrcaSlicer (loaded models, active profile, plate info).",
        {
            {"type", "object"},
            {"properties", json::object()}
        },
        "diagnostics.state"
    });

    // -----------------------------------------------------------------------
    // Viewport tools
    // -----------------------------------------------------------------------
    tools.push_back({
        "viewport_select_view",
        "Set the 3D viewport to a standard camera view preset (front, rear, top, bottom, left, right, iso, topfront).",
        {
            {"type", "object"},
            {"properties", {
                {"view", {{"type", "string"}, {"enum", json::array({"front", "rear", "top", "bottom", "left", "right", "iso", "topfront"})}, {"description", "The view direction to select"}}}
            }},
            {"required", json::array({"view"})}
        },
        "viewport.select_view"
    });

    tools.push_back({
        "viewport_zoom_to_bed",
        "Zoom the viewport to fit the entire build plate/bed in view.",
        {
            {"type", "object"},
            {"properties", json::object()}
        },
        "viewport.zoom_to_bed"
    });

    tools.push_back({
        "viewport_zoom_to_volumes",
        "Zoom the viewport to fit all objects/volumes in view.",
        {
            {"type", "object"},
            {"properties", json::object()}
        },
        "viewport.zoom_to_volumes"
    });

    tools.push_back({
        "viewport_zoom",
        "Zoom the viewport in or out by a delta amount. Positive values zoom in, negative values zoom out.",
        {
            {"type", "object"},
            {"properties", {
                {"delta", {{"type", "number"}, {"description", "Zoom delta (positive = zoom in, negative = zoom out)"}}}
            }},
            {"required", json::array({"delta"})}
        },
        "viewport.zoom"
    });

    tools.push_back({
        "viewport_camera_info",
        "Get current camera/viewport information including type, position, target, zoom level, and viewport dimensions.",
        {
            {"type", "object"},
            {"properties", json::object()}
        },
        "viewport.camera_info"
    });

    return tools;
}

}} // namespace Slic3r::GUI
