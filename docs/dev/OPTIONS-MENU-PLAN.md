# In-game PC options menu — design + resume point

Status: **implemented as the port-owned F10 overlay; this document now records
the ownership boundary and test contract.** The config-system foundation and
the overlay are both landed. The implementation is covered by deterministic
unit tests, including controller navigation, both logical viewport sizes, the
bitmap fallback clip contract, and the vector-font route.

Findings context: D180 (input), D181 (first route-(b) hook). Config API:
`configRegister{Int,UInt,Float,String}` with clamps (`port/include/config.h`).

---

## 1. What PD does, and why it doesn't port directly

PD's `pd_port/port/src/optionsmenu.c` (~2000 lines) hooks port-side handlers
into **PD's own decomp menu tables** (`menudata`/`menuitem` arrays with
`MENUITEMTYPE_SLIDER` etc.). It works because PD's front end is a generic
data-driven list menu.

GE is different. GE's settings UI is the **in-game watch** (`src/game/options.c`):
`draw_watch_game_options_page` / `draw_watch_control_options_page` +
`watch_screenN_navigation` — hand-drawn pages with bespoke nav functions,
not a table. The front-end menus (`front.c`, `initmenus.c`, `mpmenu.c`) are
also bespoke draw/nav pairs, not a reusable list widget.

So there is **no table to inject rows into**. A PC options surface is either
(A) a new hand-built watch page, (B) a new hand-built front-end screen, or
(C) a port-layer overlay drawn outside the game's menu system.

## 2. Recommended approach: (C) port-layer overlay

A self-contained immediate-mode overlay in `port/`, toggled by `F10` (or the
controller Select/Back equivalent), is drawn as fast3d 2D quads over the game
and navigated entirely in the port layer. Reasons:

- **Zero `src/` menu-code edits.** No new bespoke nav function to get wrong
  (the watch-nav code is exactly the D118d / over-scroll family).
- Works in-level *and* in the front end (it's above the game).
- The values it edits are already port-owned `config.c` variables — the
  overlay is just a view over the registered option list.
- Precedent: the port already draws port-owned UI (FPS in the window title,
  F12 screenshot). This is the render-side equivalent.

The landed widget layer uses explicit row metadata over the `config.c` registry
so display labels, cycles, slider steps, hidden dependent rows, and restart
markers remain readable and testable. The registry supplies the live pointer,
type, and clamp bounds.

### Sketch

```
port/src/optionsoverlay.c
  optionsOverlayToggle()        // F10 in video.c event pump
  optionsOverlayHandleInput()   // called from inputComputePad idx 0 when open:
                                //   swallow kbd/mouse from the game, drive the cursor
  optionsOverlayRender(Gfx**)   // called from a fast3d 2D hook after the game DL
```

The landed registry additions are `configForEachOption(cb)` and a metadata
side table keyed by dotted option name. This keeps config persistence generic
while the overlay owns presentation labels and enum names.

Live-apply: most keys already re-read their variable every frame
(`mouseAimSpeed`, `portScreenShakeScale`, `cfgVSync` via `videoSetVSync`).
Ones that need a hook (MSAA, fullscreen) get an `configOnChange` callback
or are flagged "restart".

## 3. Shipped option set for v1

| Row | Key | Type | Notes |
|---|---|---|---|
| Fullscreen / resolution | `Video.Fullscreen`, `__Resolution` | toggle / cycle | window request |
| VSync / frame cap | `Video.VSync`, `Video.FpsCap` | toggle / 30–60 cycle | live |
| Render mode | `Video.RenderMode` | cycle | live |
| Dynamic lighting / bump maps | `Video.DynamicLighting`, `Video.BumpMaps` | toggles | live |
| MSAA | `Video.MSAA` | OFF/2x/4x/8x cycle | restart marker |
| Texture quality fixes | `Video.TextureFilter`, `Video.FixMipTextures`, `Video.WrapFix` | cycle / toggles | live |
| Scale and distance | `Video.FovScale`, `Video.DrawDistance`, `Video.LodDistance` | sliders | live |
| Automatic scale policies | `Video.WidescreenAuto`, `Video.DrawDistanceAutoFov`, `Video.LodDistanceAutoFov` | toggles | live |
| Post processing | `Video.PostFX`, `Video.Sharpen`, `Video.DepthOfField` | toggle / slider / toggle | live |
| Other video | `Video.Anisotropy`, `Video.SafeAreaCrop` | slider / toggle | live |
| Input/game convenience | `Input.MouseSensitivity`, `Input.MouseInvertY`, `Input.PadLookInvertY`, `Game.NoHitFlash`, `Game.AllUnlocked` | slider/toggles | live or next launch |
| Quit | `__QuitToDesktop` | action | exits |

`configSave()` on overlay close.

## 4. Feasibility of assigning PC graphics settings to the watch menu

Recommendation: keep PC graphics settings in the F10 overlay. Do not add the
graphics rows directly to the existing watch menu.

The watch menu is game-owned (`src/game/options.c`) and its five pages are
hand-drawn, localized N64 UI with game-state setters and save-data semantics.
The F10 rows are port-owned host settings: resolution, MSAA, renderer mode,
post-processing, bump maps, texture repair toggles, and draw/LOD distance.
Those values may be changed in the front end or during a level and some of
them require SDL/OpenGL invalidation or a live-renderer request. Putting them
in the watch menu would couple `src/game` to the host config registry and
would need a new bridge for ownership, persistence, apply timing, and layout.
It would also make the N64 watch-menu path harder to keep verbatim.

Controller inversion is a separate, lower-cost question but still needs one
source of truth. The watch menu's `LOOK_UPDOWN` option is a game/player
setting (`get_cur_player_look_vertical_inverted()`); `Input.PadLookInvertY` is
the port's SDL right-stick mapping setting. They should not be presented as
two independent toggles that can disagree. If the product requirement is to
make inversion discoverable in the watch menu, the appropriate future design
is one narrow port bridge that edits the same `Input.PadLookInvertY` value,
with a test proving both surfaces read and write the same setting. It is not
appropriate to move the graphics rows into that menu.

A future watch-menu entry labelled “PC graphics…” could be reasonable as a
launcher for the existing overlay, but it should call a port-owned callback
and leave all row state, rendering, and persistence in `port/`. That bridge
would need its own navigation and “overlay owns controller 0” tests before it
is enabled.

## 5. Text/rendering test contract

The overlay has two valid text paths:

- enhanced/remaster mode queues printable menu text into the current frame's
  vector batch; the late queue is required because the overlay is emitted
  after the game's display-list build;
- when vector text is unavailable or its batch is full, the bitmap path uses
  the full VI viewport as the clip rectangle and uses bitmap-font metrics for
  right-aligned values and centered controls. Passing a measured string
  width/height there clips glyphs, or using vector advances for bitmap output
  misplaces values, making the menu look blank or corrupted.

`ge007_options_overlay` tests both paths at the in-level and front-end logical
viewport sizes, checks row baselines do not overlap, and drives the graphics
rows with a fake controller A edge. `ge007_vector_text_frame` separately pins
the current-versus-pending batch boundary. A real GPU screenshot remains a
useful final visual check, but it is not needed to establish these deterministic
unit contracts.

## 6. Alternative if an in-fiction surface is required later

Route (B): a new front-end screen `constructor_menuXX_pcoptions` +
`menuXX_pcoptions_navigation` in `front.c` under `#ifdef PORT`, reached from
a new "PC OPTIONS" row on the main options menu. Each row calls a
`configGet/Set` shim. This is a genuine route-(b) `src/` edit (menu content
change) → its own Dxx, N64 verbatim under `#else`, opt-in. More faithful,
more surface area, more nav bugs. Defer unless the owner wants it in-fiction.

## 7. Follow-up checklist

1. Keep `ge007_options_overlay` and `ge007_vector_text_frame` green when rows,
   labels, or frame ownership change.
2. If a watch-menu launcher is desired, add it as a separate port bridge with
   tests for ownership, controller swallowing, close/resume, and shared
   controller-inversion state.
3. Perform one bounded live screenshot check at both an in-level and a
   front-end screen. The deterministic tests cover the layout and font
   routing; they do not replace a GPU-specific visual check.
