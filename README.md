# Inner Glow Effect

# FOR EDITORS 
1. Windows: close After Effects, copy InnerGlowEffect.aex into ...\Support Files\Plug-ins\Effects\, approve the admin prompt, reopen AE.
2. Mac user command to run : xattr -dr com.apple.quarantine "/Applications/Adobe After Effects 2025/Plug-ins/Effects/InnerGlowEffect.plugin"
3. Mac: close After Effects, copy InnerGlowEffect.plugin into /Applications/Adobe After Effects <version>/Plug-ins/Effects/, run the xattr command above, reopen AE.
   Find it under Effect → Stylize → Inner Glow Effect.

# FOR CONTRIBUTIORS
After Effects' Inner Glow is a **Layer Style**, not an effect. That means two things
get in the way:

- Layer Styles always render *after* every effect on the layer, and cannot be
  reordered. Whatever your effect stack does, the glow lands on top of it.
- They live buried in the timeline under Layer Styles, not in Effect Controls.

Inner Glow Effect does the same job as a real effect. It sits in the effect stack, so it
can be placed anywhere in the chain, and it composites against whatever alpha exists
at that point rather than against the finished layer.

Effect menu location: **Effect > Stylize > Inner Glow Effect**

By [draexon](https://github.com/draexon) · [github.com/draexon/Inner-Glow-Effect](https://github.com/draexon/Inner-Glow-Effect)

---

## Parameters

Matching the Inner Glow layer style, in the same order.

| Parameter | Range | Default | Notes |
|---|---|---|---|
| Blend Mode | 27 modes | Screen | The full After Effects list, Normal through Luminosity |
| Opacity | 0–100% | 75 | Overall strength of the glow |
| Noise | 0–100% | 0 | Dithers the glow; the pattern is pinned to the layer and does not crawl |
| Color Type | Single Color / Gradient | Single Color | Whether the glow is one colour or a ramp across its falloff |
| Color | swatch | 255, 255, 190 | The single colour, and the gradient's start |
| Color Opacity | 0–100% | 100 | A second strength control, independent of Opacity |
| End Color | swatch | 255, 128, 0 | The gradient's end. Ignored in Single Color mode |
| Gradient Midpoint | 0–100% | 50 | Where Color hands off to End Color. Ignored in Single Color mode |
| Gradient Smoothness | 0–100% | 100 | Eases the ramp between the two colours. Ignored in Single Color mode |
| Technique | Softer / Precise | Softer | How the distance to the edge is measured |
| Source | Center / Edge | Edge | Where the glow comes from |
| Choke | 0–100% | 0 | How much of the reach stays fully solid before the falloff starts |
| Size | 0–250 px | 5 | How far the glow reaches inward, in full-resolution pixels |
| Range | 1–100% | 50 | Remaps the falloff curve; 50 is neutral |

The three gradient controls stay visible in Single Color mode but do nothing. After
Effects can grey out parameters, but only via `PF_OutFlag_SEND_UPDATE_PARAMS_UI`,
which would mean another out-flag to keep synchronised between the code and the PiPL.

**Not included:** multi-stop gradients and Jitter. The layer style's gradient editor
takes arbitrary stops, which needs a custom parameter UI built on arbitrary-data
params and Drawbot. This uses two colours and a midpoint instead. Jitter only
randomises stops within such a gradient, so it has nothing to act on here.

### How the gradient maps

The gradient is painted **across the glow's falloff**, not across the layer. Position
0 is where the glow is at full strength and position 1 is where it has faded out, so:

- **Source = Edge**: Color sits against the alpha edge, End Color at Size pixels in.
- **Source = Center**: Color sits deep inside the shape, End Color out at the edge.

**Range** decides which stretch of the falloff the glow occupies, so it carries the
gradient with it. **Noise** dithers strength only and never changes which colour a
pixel receives.

---

## Prerequisites

### Both platforms

- **CMake 3.20 or newer** — <https://cmake.org/download/>
- **After Effects SDK** — see below

### Windows

- **Visual Studio 2022** with the *Desktop development with C++* workload.
  Build Tools 2022 is enough; the full IDE is not needed.
- x64 only.

### macOS

- **Xcode command line tools**: `xcode-select --install`.
  This provides clang and, importantly, `Rez`, which compiles the PiPL resource.
- Builds a universal binary, arm64 and x86_64, deployment target 11.0.

---

## Getting the SDK

The After Effects SDK is **not redistributable**, so it is not vendored into this
repository and never should be.

1. Go to <https://developer.adobe.com/after-effects/> and click **Download the SDK**.
   A free Adobe ID is required; the download is behind a sign-in.
2. Pick the SDK matching your After Effects version.
3. Unpack it. On Windows the download may arrive as a zstd-compressed archive with a
   `extractzstd.bat` next to it; run that first to get the real SDK folder.
4. You want the folder that **directly contains `Examples/`**. Confirm that this
   file exists:

   ```
   <AE_SDK_PATH>/Examples/Headers/AE_Effect.h
   ```

### Setting AE_SDK_PATH

**Windows**

```bat
setx AE_SDK_PATH "D:\SDK\AfterEffectsSDK"
```

`setx` only affects *new* terminals. Open a fresh one before building.

**macOS**

```bash
export AE_SDK_PATH="$HOME/AfterEffectsSDK"
```

Add that line to `~/.zshrc` to make it persist.

Alternatively, skip the environment variable and pass it to CMake directly:

```bash
cmake -S . -B build -DAE_SDK_PATH=/path/to/AfterEffectsSDK
```

If the SDK cannot be found, configuration stops with an error saying exactly which
path it looked at. It never falls back to a stub.

---

## Building

### Windows

```bat
build.bat
```

That configures and builds Release. `build.bat Debug` builds Debug. The result is:

```
build\Release\InnerGlowEffect.aex
```

### macOS

```bash
./build.sh
```

The result is:

```
build/Release/InnerGlowEffect.plugin
```

### Doing it manually

```bash
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

On macOS substitute `-G Xcode`.

---

## Installing

```bash
cmake --install build --config Release
```

This copies the plug-in into the After Effects plug-ins folder. **After Effects must
be closed** — it holds the binary locked while running.

Default destinations:

| Platform | Folder |
|---|---|
| Windows | `C:\Program Files\Adobe\Adobe After Effects 2025\Support Files\Plug-ins\Effects\` |
| macOS | `/Applications/Adobe After Effects 2025/Plug-ins/Effects/` |

Both are inside protected locations, so the install needs elevation: an Administrator
prompt on Windows, `sudo` on macOS.

To install somewhere else, or for a different After Effects version:

```bash
cmake -S . -B build -DAE_PLUGIN_INSTALL_DIR="C:/path/to/Plug-ins/Effects"
```

`build.bat Release install` and `./build.sh Release install` build and install in one
step.

---

## Verifying it loaded

1. Launch After Effects.
2. Create a comp with any layer that has transparency — a solid with a mask, a shape
   layer, or text works well. A full-frame solid with no transparency will show
   nothing, because an inner glow needs an alpha edge to grow from.
3. Look for **Effect > Stylize > Inner Glow Effect**. If it is not in the menu, the PiPL
   did not build or the binary is not in a folder After Effects scans.
4. Apply it. You should see a soft glow hugging the inside of the layer's alpha edge.
5. Push **Size** up to make the effect obvious, then try **Source > Center** to flip
   the glow to the middle of the shape.
6. Switch **Color Type** to **Gradient** to ramp the glow from Color to End Color
   across its falloff.

The real point of this plug-in: drag it above or below other effects in the stack and
watch the glow re-composite against whatever alpha exists at that point. The built-in
Layer Style cannot do that.

---

## How it works

### The glow field

The glow is not a per-pixel effect. Every pixel's result depends on how far it sits
from the nearest transparent pixel, so each render builds a coverage field across the
whole input before touching any output pixel.

**Precise** runs an exact Euclidean distance transform (Felzenszwalb and
Huttenlocher, linear in pixel count). The falloff follows the shape's corners tightly,
and a pixel at a given depth glows the same whether it is behind a flat edge or a
corner.

**Softer** blurs the alpha instead, with sigma set to a third of Size, so the glow
fades out right around Size pixels in. Blurring accumulates transparency from every
direction at once, which is what makes inside corners come out brighter and the whole
falloff round off.

Both make the same half-pixel correction at the edge, so switching Technique does not
change how bright the outermost pixel is.

### Premultiplied alpha

After Effects hands over premultiplied pixels. The render unpremultiplies, does all
colour maths on straight colour, then premultiplies back. Skipping that is what makes
overlays darken and fringe along antialiased edges.

Confining the glow to the existing layer content falls out of re-premultiplying by the
original alpha: anything outside is multiplied by zero. The glow field itself is
deliberately *not* masked by alpha, because multiplying by alpha a second time would
fade the glow across antialiased edges, which is exactly where it should be strongest.

Alpha is passed through untouched, always. Fully transparent pixels keep whatever RGB
they arrived with.

### Tiles and margins

Under smart render After Effects asks for regions, not whole frames. A pixel near the
edge of a region cannot see the alpha just outside it, so `PF_Cmd_SMART_PRE_RENDER`
asks for an input rect grown by the glow's reach. Without that, the glow would be
wrong along every tile boundary.

### Threading

`PF_OutFlag2_SUPPORTS_THREADED_RENDERING` is set, so After Effects may drive the
render from several threads and several frames at once. Nothing in the render path
holds state: no globals are written, no static caches exist, and every buffer is
allocated and freed inside the call that uses it.

`PF_OutFlag_PIX_INDEPENDENT` is deliberately **not** set. Results are not independent
of their neighbours, and claiming otherwise would let After Effects render alternate
rows and interpolate the rest.

### Bit depth

8, 16 and 32 bits per channel are all supported. At 32 bpc the result is left
unclamped so overrange values survive.

---

## A warning worth repeating

The out-flags in `src/InnerGlowEffect.cpp` (`GlobalSetup`) and the ones in
`resources/InnerGlowEffect_PiPL.r` must stay identical. After Effects reads the PiPL when
it scans the plug-in and **never warns** when the two disagree; it just misbehaves
quietly, with the wrong bit depth, no smart render, or threading crashes. Both files
carry a comment saying so, with the bit-by-bit breakdown.

Current values:

```
out_flags  = 0x02000000   DEEP_COLOR_AWARE
out_flags2 = 0x08001400   SUPPORTS_SMART_RENDER | FLOAT_COLOR_AWARE | SUPPORTS_THREADED_RENDERING
```

## Do not change the match name

```
DRAEXON InnerGlowEffect
```

After Effects identifies the effect by this string. Changing it after release orphans
the effect in every project that already uses it. The same goes for parameter order:
values are stored by index, so parameters may only be appended, never reordered or
removed.

---

## Layout

```
CMakeLists.txt                      one project, both platforms
LICENSE                             MIT
build.bat                           Windows one-liner
build.sh                            macOS one-liner
src/
  InnerGlowEffect.h
  InnerGlowEffect.cpp                   entry point, param setup, command dispatch
  GlowRender.h
  GlowRender.cpp                    glow field and blend modes, pure functions
resources/
  InnerGlowEffect_PiPL.r            PiPL resource, both platforms
  InnerGlowEffect_Version.rc        Windows version resource
  Info.plist.in                     macOS bundle template
```

---

## Licence and credit

MIT. See [LICENSE](LICENSE). Use it, change it, ship it, sell it. The one condition
is that the copyright notice travels with any copy or substantial portion.

Built by **draexon** — <https://github.com/draexon/Inner-Glow-Effect>

Authorship is recorded in four places so it survives being passed around:

| Where | How to see it |
|---|---|
| Windows version resource | Right-click `InnerGlowEffect.aex` → Properties → Details |
| About box | Effect Controls → the effect's menu → About |
| PiPL support URL | After Effects surfaces it for the effect |
| Match name | `DRAEXON InnerGlowEffect`, stored in every project that uses it |

To be clear about what that is and is not: it is attribution, not protection. Anyone
holding the binary can edit those strings, and MIT permits closed commercial forks
outright. The only thing that cryptographically proves who built a given binary is
code signing, an Authenticode certificate on Windows or a Developer ID on macOS.
Neither is set up here.

The match name is the stubborn one. Changing it orphans the effect in every project
that already uses it, so a rebrand is not free.
