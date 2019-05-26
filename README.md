# wlay
Graphical output management for Wayland. Work in progress.

![screenshot](https://user-images.githubusercontent.com/3966931/58381331-3e75d680-7fbc-11e9-9199-bb49e26670b7.png)

## Building

```
$ mkdir build
$ cd build
$ cmake ..
$ make
$ ./wlay
```

## Usage

Hold `TAB` to enable edge snapping. `Apply` sends the configuration to the window manager. `Save` can generate [sway](https://github.com/swaywm/sway) config or [wlr-randr](https://github.com/emersion/wlr-randr) script.
