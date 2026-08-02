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

**Still missing (blocking next step):** the physical row-to-row spacing in
mm. The Espressif docs page links a "Dimensions" PDF/DXF that wasn't fetched
yet — need to pull the actual number from there rather than guess, since a
wrong header-pitch guess produces a board where the devkit doesn't fit.

## Repo placement (not yet decided/created)

Planned to put the KiCad project under something like
`hardware/focusdock-pcb/` in this repo (`time-prod`), matching the existing
`docs/` convention — not yet created.

## TODO / next steps

1. Get exact J1↔J3 row spacing (and board width/length) from Espressif's
   dimensions PDF/DXF, or from a second source that states it in mm
   (community sites like espboards.dev / mischianti.org may quote it more
   directly than Espressif's own page did).
2. `mcp__kicad__create_project` under `hardware/focusdock-pcb/`.
3. Build the schematic:
   - Two 1×22 female header symbols for the devkit socket (J1/J3), or a
     single custom symbol if that's cleaner.
   - 1×4 header for the OLED (VDD, GND, SCK, SDA).
   - Header(s) for the BMI160 breakout (VIN, 3V3-unconnected, GND, SCL, SDA,
     CS, SA0, plus the unused aux pins broken out but left NC per
     `docs/HARDWARE.md`).
   - 1×3 header for the WS2812B strip (VDD, GND, DIN), with a 330–470 Ω
     series resistor in the DIN line (per the electronics doc — protects the
     first LED).
   - 1×6 header for the 5-way button (Up/Down/Left/Right/Click/Common-GND).
   - Wire nets exactly per the wiring table in `docs/HARDWARE.md`:
     GPIO4/5/6/7/15 → buttons, GPIO8/9 → I2C SDA/SCL shared by OLED+IMU,
     GPIO16 → resistor → LED DIN, IMU CS tied to 3V3, IMU SA0 tied to GND,
     BMI160 `3V3` pin left unconnected (it's a regulator output, not input).
   - Run ERC.
4. `sync_schematic_to_board`, place footprints (leave room for the
   monitor-clip/phone-dock mechanical envelope mentioned in the README
   roadmap — no fixed size chosen yet), route traces, GND copper pour,
   mounting holes.
5. Run DRC, export a 2D board preview + schematic SVG so the user can
   sanity-check without opening KiCad, then have the user open the real
   `.kicad_pro` in KiCad to verify.

## Session bookkeeping

Task tool IDs in use (`TaskList` in this session):
- #1 Install KiCad via Homebrew — completed
- #2 Configure KiCAD MCP server for this session — in progress (basically
  done; can be marked completed)
- #3 Create FocusDock KiCad project (carrier board) — pending
- #4 Lay out and route FocusDock PCB — pending
