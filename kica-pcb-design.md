# FocusDock KiCad PCB — Setup Log & Plan

Goal: replace the breadboard wiring in `docs/HARDWARE.md` with a real KiCad
project the user can open and verify — a **carrier board** with headers/
connectors for the existing breakout modules (not a from-scratch redesign
with bare ICs).

## Decisions made

- **Approach:** carrier board with headers — keep the ESP32-S3 devkit, OLED
  breakout, BMI160 breakout, 5DirKey button, and WS2812B strip as-is; the PCB
  just replaces the jumper wires between them.
- **KiCad version:** target KiCad 8-compatible output — but see below, we
  ended up installing **KiCad 10.0.5** (current stable as of Aug 2026)
  instead, since that's what `brew install --cask kicad` provides and the MCP
  server writes files matching whatever KiCad version generated them.

## Environment setup (done)

1. Cloned the KiCad MCP server (`mixelpixx/KiCAD-MCP-Server`) — a Node/
   TypeScript MCP server that shells out to a Python backend using KiCad's
   own `pcbnew` module to actually create/edit `.kicad_pro` / `.kicad_sch` /
   `.kicad_pcb` files.
   - Moved it to a **permanent** location (NOT the session scratchpad, which
     gets wiped): `~/mcp-servers/KiCAD-MCP-Server`
   - `npm install` (runs the TypeScript build via its `prepare` script) →
     `dist/index.js` exists.
2. Installed KiCad itself via `brew install --cask kicad` (user ran this
   manually in an interactive terminal because the cask needs `sudo` for
   `/Library/Application Support/kicad`, which a background shell can't
   prompt for). Installed at `/Applications/KiCad/KiCad.app`, version 10.0.5.
3. Created a Python venv using KiCad's *bundled* Python (required so
   `pcbnew` — a compiled C++ extension shipped inside the KiCad app — is
   importable):
   ```bash
   cd ~/mcp-servers/KiCAD-MCP-Server
   /Applications/KiCad/KiCad.app/Contents/Frameworks/Python.framework/Versions/Current/bin/python3 \
     -m venv venv --system-site-packages
   source venv/bin/activate
   pip install -r requirements.txt
   ```
   Verified: `venv/bin/python3 -c "import pcbnew, skip, PIL, cairosvg, pydantic"` → OK
   (note: the `kicad-skip` PyPI package imports as `skip`, not `kicad_skip`).
4. Registered the MCP server with Claude Code (**local** scope — this
   project only, not committed):
   ```bash
   claude mcp add kicad -s local \
     -e NODE_ENV=production \
     -e PYTHONPATH="/Applications/KiCad/KiCad.app/Contents/Frameworks/Python.framework/Versions/Current/lib/python3.9/site-packages" \
     -e LOG_LEVEL=info \
     -e KICAD_AUTO_LAUNCH=false \
     -- node "/Users/ibadrather/mcp-servers/KiCAD-MCP-Server/dist/index.js"
   ```
   This lives in `~/.claude.json` under the project entry — not part of the
   git repo.
5. User reconnected via `/mcp` so the new server's ~146 tools loaded into
   the session (`mcp__kicad__*`).

**Status: environment is fully working.** `mcp__kicad__list_symbol_libraries`
and `mcp__kicad__search_footprints` both returned real results against
KiCad's bundled libraries, e.g.:
- `RF_Module:ESP32-S3-WROOM-1` (bare module footprint — not what we need for
  the carrier-board approach, see below)
- No bundled footprint exists for the *devkit board itself*
  (`ESP32-S3-DevKitC-1`) — only the bare WROOM-1 module. Same for symbols:
  `search_symbols("ESP32-S3-DevKitC")` → no results.

## Design approach for the ESP32-S3-DevKitC-1 devkit

Since there's no ready-made KiCad footprint for the whole devkit board, plan
is to model it as **two 1×22 female pin sockets** (2.54 mm pitch) at the
correct row-to-row spacing, so the real devkit plugs into the carrier board
like a shield. This requires the *real* mechanical spacing between the two
header rows (J1/J3) — getting this wrong means the devkit physically won't
seat.

Confirmed pinout order (from Espressif's official user guide,
`docs.espressif.com/.../esp32-s3-devkitc-1/user_guide_v1.1.html`):

- **J1** (pin 1→22): `3V3, 3V3, RST, 4, 5, 6, 7, 15, 16, 17, 18, 8, 3, 46, 9, 10, 11, 12, 13, 14, 5V, G`
- **J3** (pin 1→22): `G, TX, RX, 1, 2, 42, 41, 40, 39, 38, 37, 36, 35, 0, 45, 48, 47, 21, 20, 19, G, G`

**Resolved.** Fetched Espressif's mechanical DXF
(`DXF_ESP32-S3-DevKitC-1_V1.1_20220429.dxf`) and measured the drill geometry
directly rather than trusting a quoted figure — the header holes live on the
`TOOL_BOT` layer:

| Measurement | Value |
|---|---|
| J1 row | x = 1.270 mm |
| J3 row | x = 24.130 mm |
| **Row-to-row spacing** | **22.860 mm** (0.900 in) |
| Pin pitch | 2.540 mm (22 pins, y 7.960 → 61.300) |
| Drill | Ø1.02 mm |
| Board outline | 25.40 × 62.74 mm |

Pin 1 of both rows is at the antenna end; the USB ports are at the pin-22 end.
(Note the widely-quoted "70 × 28 mm" board size on community sites is wrong —
the drawing's own dimension annotations say 62.74 × 25.40 mm.)

## Repo placement

Created at `hardware/focusdock-pcb/`, matching the existing `docs/`
convention. See [`hardware/focusdock-pcb/README.md`](hardware/focusdock-pcb/README.md)
for the board's own documentation.
## Board as built

`hardware/focusdock-pcb/focusdock-carrier.*` — 70 × 50 mm, 2 layers, GND pour
on both sides. **ERC 0/0. DRC 0 errors, 0 unconnected pads.**

Connectors: J1/J3 devkit sockets (1×22, 22.86 mm apart), J2 OLED (1×4), J4 IMU
(1×12), J5 LED strip (1×3), J6 5DirKey (1×6). Passives: R1 330 Ω in the LED
data line, C1 100 nF + C2 10 µF on 3V3, C3 100 µF on the LED rail. JP1 is a
3-pad solder jumper selecting the LED supply between 3V3 (default, bridged)
and 5V — the PCB expression of the "3.3 V for testing, 5 V later" note in
`docs/HARDWARE.md`.

Two decisions worth recording, because they drove the layout:

- **Every peripheral connector sits above the devkit footprint**, not below it.
  The devkit body covers y 38.7–64.1, so anything between the socket rows would
  be unreachable once it is seated. Putting the connectors in the strip above
  J1 also means no signal has to cross a pad row.
- **Connector pin order was chosen to match J1's pin order left-to-right**
  (+3V3, buttons, LED, SDA, SCL, +5V). J2 and J5 are rotated 180° for exactly
  this reason. That made almost every net a short monotonic run and removed the
  crossings that otherwise force vias — the buttons ended up as five straight
  8 mm traces.

Freerouting was unavailable (no Java 21, no Docker, no jar), so routing is by
hand: signals on F.Cu, I2C and the 3V3 backbone on B.Cu, GND by pour.

Generated output lives in `hardware/focusdock-pcb/export/` (SVG + PNG previews)
and `fab/` (gerbers, Excellon drill, BOM, pick-and-place).

## Still open

- **J4 pin order is an assumption.** Wired for the common GY-BMI160 12-pin
  order (`VIN NC GND SCL SDA SA0 CS NC NC NC NC NC`). Verify against the actual
  module's silkscreen before ordering — the order is printed on the back
  silkscreen, so it can be checked against a bare board too.
- No enclosure dimensions were available, so the outline is a plain rounded
  rectangle sized to the devkit. The monitor-clip / phone-dock envelope in the
  README roadmap is not accounted for.
- Spare devkit GPIOs are left unconnected rather than broken out to an
  expansion header — worth reconsidering when the presence sensor on the
  roadmap lands.
- The 74AHCT125 level shifter and the 1000 µF bulk cap remain roadmap items;
  neither is on this board.
- Four `MountingHole_2.7mm` "does not match copy in library" DRC warnings are
  cosmetic (locally created footprint variants).
