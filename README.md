# ReplayModule

Records what happens on the minimap as compact frames and plays the match back afterwards through a
slider-driven, material-styleable widget.

This repository contains the source files for the ReplayModule Unreal Engine plugin. If you need the
full plugin project including the example widgets, materials and content, you're in the right place.

The plugin is **standalone** - nothing in it requires another plugin. When the
[RTSUnitTemplate](https://github.com/SilvanTeufel/RTSUnitTemplate) plugin is installed next to it,
the recorder discovers the minimap bounds, the team colors and the win/lose screen by itself, and
RTSUnitTemplate is not modified in any way.

## Download Link for the Full Plugin Project

If you require a download link for the complete plugin project, please send an email to
[info@teufel-engineering.com](mailto:info@teufel-engineering.com), and I will provide you with a
Google Link containing the plugin content, example blueprints and materials.

## Usage

With these source files you can create a plugin on your own and replace the existing source folder.
To set up:

1. Right-click on the Project File.
2. Choose "Generate Visual Studio Project Files".
3. Compile the plugin in the Unreal Engine Editor.
4. Once successfully compiled, you can create blueprints from these classes.

Remember, if you purchase the plugin from the Fab Store, it already includes example widgets and
materials for a more seamless setup.

## How it works

| Piece | Role |
| --- | --- |
| `UReplayRecorderSubsystem` (world) | Samples a frame every `RecordIntervalSeconds`. |
| `UReplayStorageSubsystem` (game instance) | Owns the finished recording, so it survives the ServerTravel a win/lose screen triggers. |
| `UReplayFrameRenderer` | Draws one frame into a texture - fog, revealed areas, markers, camera rectangle. |
| `UReplayWidget` | The replay window: picture, scrub slider, speed button. |
| `UReplayLauncherWidget` | The small "Watch Replay" button that appears when the match ends. |
| `UReplayMarkerComponent` | Opt-in marker for projects that do not use RTSUnitTemplate. |
| `UReplayFunctionLibrary` | The whole plugin as Blueprint nodes. |

A frame is a list of 8-byte dots (quantized position, dot radius, sight radius, palette index) plus
an optional camera quad. At one frame per second a normal match lands in the low single-digit
megabytes, so a whole game can be kept in memory and written to a save game.

Settings live under Project Settings > Plugins > Replay Module: recording interval, playback speeds,
playback resolution, fog-of-war mode, the widget classes to use, and optional auto-save to a slot.

## Styling

The replay window works with no assets at all: it stacks the terrain texture and the marker texture
as plain images, which already looks like the live minimap because revealed areas are transparent.

To skin it, either set the material slots on the C++ class or make a Blueprint subclass of
`UReplayWidget`:

* `MapMaterial` - the picture. Receives the marker layer as `MinimapTexture` and the terrain layer as
  `TopographyTexture` (parameter names are configurable), so an RTS project can point this at its
  existing minimap material.
* `FrameMaterial` - a decorative frame drawn over the picture, never hit-testable.
* `BackdropMaterial` / `BackdropColor` - the panel behind everything.

A Blueprint subclass takes over the layout completely as soon as it has its own widget tree. Name the
widgets `MapImage`, `BackgroundImage`, `FrameImage`, `TimeSlider`, `PlayPauseButton`, `PlayPauseText`,
`SpeedButton`, `SpeedText`, `CloseButton`, `TimeText`, `TitleText`, `RootBorder` and they get bound -
all of them are optional.

## Using it without RTSUnitTemplate

1. Call `StartReplayRecording` with the world bounds you want to cover.
2. Put a `UReplayMarkerComponent` on everything that should appear, or bind `OnCaptureFrame` on the
   recorder and call `PushDot`.
3. Call `NotifyGameEnded` when the match is over.

## Documentation

For a detailed understanding of the classes, refer to the documentation available at
[wiki.teufel-engineering.com](http://wiki.teufel-engineering.com).

## ⚖️ License & Usage (Source-Available)

This repository operates under a **Dual-Licensing Model** (Free for Non-Commercial / Paid for
Commercial).

* **Free for Non-Commercial Use:** You can download, fork, modify, and use this plugin entirely for
  free in your personal, educational, and non-monetized projects.
* **Paid for Commercial Use:** If you plan to sell your game or monetize it in any way (including
  microtransactions, ads, or Patreon), you **must** purchase a commercial license from the official
  Fab Store.

See [LICENSE.txt](LICENSE.txt) for the full terms.
