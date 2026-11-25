#!/usr/bin/env bash
# Fork smoke test: slices a tiny model with the freshly built binary and checks that fork features
# still change the output. The merge step only checks that fork code is still present; this checks
# that it still works, which a clean upstream merge at an override site can silently break.
#
# Covers FORK(gap-fill-override): gap_fill_target_override, globally and per part.
#
# usage: fork_smoke_test.sh <orca-slicer binary>
set -u

BIN="${1:-}"
[ -x "$BIN" ] || { echo "fork smoke test: binary not found: $BIN"; exit 2; }
PY="${PYTHON:-python3}"

WORK="$(mktemp -d "${TMPDIR:-/tmp}/orca-fork-smoke.XXXXXX")"
trap 'rm -rf "$WORK"' EXIT
mkdir -p "$WORK/datadir"
cd "$WORK" || exit 2

# Standalone presets: without "inherits" the CLI loads them as-is, with no preset bundle.
cat > machine.json <<'EOF'
{"type":"machine","from":"User","name":"fork smoke printer","printable_area":["0x0","200x0","200x200","0x200"],"printable_height":"100","layer_change_gcode":"G92 E0"}
EOF

# 10 layers, all solid: layer 1 is the bottom surface, 10 the top surface, 2-9 internal solid infill.
# The 0.6 mm first layer line leaves gaps for gap fill on layer 1 as well.
process() { # name gap_fill_target gap_fill_target_override
    cat > "$1.json" <<EOF
{"type":"process","from":"User","name":"fork smoke $1","layer_height":"0.2","initial_layer_print_height":"0.2",
 "initial_layer_line_width":"0.6","top_shell_layers":"5","bottom_shell_layers":"5","top_shell_thickness":"0",
 "bottom_shell_thickness":"0","wall_loops":"2","wall_generator":"arachne","enable_support":"0","skirt_loops":"0",
 "brim_type":"no_brim","filter_out_gap_fill":"0","gap_fill_target":"$2","gap_fill_target_override":"$3"}
EOF
}

# A long thin wedge (12 x 60 x 2 mm): solid infill cannot reach into the pointed tip, so the slicer
# fills it with gap fill wherever gap fill is allowed.
wedge() { # file x_offset
    "$PY" - "$1" "$2" <<'EOF'
import sys
x0 = float(sys.argv[2])
poly, z0, z1 = [(x0, 0), (x0 + 12, 0), (x0 + 6, 60)], 0.0, 2.0
tris = [((*poly[0], z0), (*poly[2], z0), (*poly[1], z0)), ((*poly[0], z1), (*poly[1], z1), (*poly[2], z1))]
for i in range(3):
    a, b = poly[i], poly[(i + 1) % 3]
    tris += [((*a, z0), (*b, z0), (*b, z1)), ((*a, z0), (*b, z1), (*a, z1))]
with open(sys.argv[1], "w") as f:
    f.write("solid wedge\n")
    for t in tris:
        f.write("facet normal 0 0 0\nouter loop\n")
        for p in t:
            f.write("vertex %g %g %g\n" % p)
        f.write("endloop\nendfacet\n")
    f.write("endsolid wedge\n")
EOF
}

# Prints the layers that carry gap fill, as "left:<layers> right:<layers>", split at the middle of
# the printed area (the two parts of the per-part case sit side by side along X).
gap_layers() { # gcode
    "$PY" - "$1" <<'EOF'
import re, sys
layer, role, pts, xs = 0, None, [], []
for line in open(sys.argv[1]):
    if line.startswith(";LAYER_CHANGE"):
        layer, role = layer + 1, None
    elif line.startswith(";TYPE:"):
        role = line[6:].strip()
    elif line.startswith("G1") and " E" in line and role and role != "Custom":
        m = re.search(r" X([-\d.]+)", line)
        if m:
            xs.append(float(m.group(1)))
            if role == "Gap infill":
                pts.append((layer, xs[-1]))
mid = (min(xs) + max(xs)) / 2 if xs else 0
side = lambda right: ",".join(map(str, sorted({l for l, x in pts if (x >= mid) == right}))) or "none"
print("left:%s right:%s" % (side(False), side(True)))
EOF
}

FAILED=0
check() { # label expected actual
    if [ "$2" = "$3" ]; then echo "  ok    $1: $3"; else echo "  FAIL  $1: expected '$2', got '$3'"; FAILED=1; fi
}

slice() { # process model outdir
    mkdir -p "$3"
    "$BIN" --datadir "$WORK/datadir" --load-settings "$WORK/machine.json;$WORK/$1.json" --slice 0 --outputdir "$WORK/$3" "$2" > "$3.log" 2>&1 \
        || { echo "  FAIL  slicing $3 exited with an error:"; tail -n 20 "$3.log"; FAILED=1; return 1; }
}

echo "fork smoke test: gap fill override"
wedge wedge_a.stl 20
wedge wedge_b.stl 40

# 1. Global. A single part prints left of the middle only on its own, so compare the union of both sides.
global_case() { # label gap_fill_target override expected_layers
    process "g_$2_$3" "$2" "$3"
    slice "g_$2_$3" "$WORK/wedge_a.stl" "out_g_$2_$3" || return
    got="$(gap_layers "out_g_$2_$3/plate_1.gcode" | "$PY" -c '
import sys
s = set()
for part in sys.stdin.read().split():
    v = part.split(":")[1]
    if v != "none": s |= set(map(int, v.split(",")))
print(",".join(map(str, sorted(s))) or "none")')"
    check "$1" "$4" "$got"
}
ALL="1,2,3,4,5,6,7,8,9,10"
global_case "stock, everywhere (sanity: this model produces gap fill)" everywhere inherit "$ALL"
global_case "stock, nowhere" nowhere inherit none
global_case "override everywhere beats apply=nowhere" nowhere everywhere "$ALL"
global_case "override nowhere beats apply=everywhere" everywhere nowhere none
global_case "override top" everywhere top 10
global_case "override bottom" everywhere bottom 1

# 2. Per part: two wedges in one object, each with its own override in the project file.
process p_nowhere nowhere inherit
process p_everywhere everywhere inherit
if "$BIN" --datadir "$WORK/datadir" --load-settings "$WORK/machine.json;$WORK/p_nowhere.json" --assemble \
        --export-3mf "$WORK/two.3mf" "$WORK/wedge_a.stl" "$WORK/wedge_b.stl" > assemble.log 2>&1 \
        && mkdir two && (cd two && unzip -q ../two.3mf); then
    part_case() { # label process override_a override_b expected
        rm -rf "proj_$3_$4"; cp -R two "proj_$3_$4"
        "$PY" - "proj_$3_$4/Metadata/model_settings.config" "$3" "$4" <<'EOF'
import sys
path, a, b = sys.argv[1:]
s = open(path).read()
for name, value in (("wedge_a.stl", a), ("wedge_b.stl", b)):
    key = '<metadata key="name" value="%s"/>\n' % name
    assert s.count(key) == 1, "part %s not found in the project" % name
    if value != "-":
        s = s.replace(key, key + '      <metadata key="gap_fill_target_override" value="%s"/>\n' % value)
open(path, "w").write(s)
EOF
        (cd "proj_$3_$4" && zip -q -r -X "../proj_$3_$4.3mf" '[Content_Types].xml' _rels 3D Metadata)
        slice "$2" "$WORK/proj_$3_$4.3mf" "out_p_$2_$3_$4" || return
        check "$1" "$5" "$(gap_layers "out_p_$2_$3_$4/plate_1.gcode")"
    }
    part_case "per part: only the right part overridden to everywhere" p_nowhere - everywhere "left:none right:$ALL"
    part_case "per part: left top, right bottom" p_everywhere top bottom "left:10 right:1"
    part_case "per part: left nowhere, right inherits everywhere" p_everywhere nowhere - "left:none right:$ALL"
else
    echo "  FAIL  could not build the two-part project:"; tail -n 20 assemble.log; FAILED=1
fi

if [ "$FAILED" -ne 0 ]; then
    echo "fork smoke test FAILED: a fork feature no longer changes the slicer's output (see CLAUDE.md, 'This Fork')."
    exit 1
fi
echo "fork smoke test passed"
