# wlay
Graphical output management for Wayland.

![screenshot](https://user-images.githubusercontent.com/3966931/58671625-8e99c380-8343-11e9-9a8e-df6c3896eb45.png)

## Building

You need the wayland client libraries, extra-cmake-modules, glfw3 and libepoxy.

```
$ mkdir build
$ cd build
$ cmake ..
$ make
$ ./wlay
```

## Usage

Hold `TAB` to enable edge snapping. `Apply` sends the configuration to the window manager. `Save` can generate [sway](https://github.com/swaywm/sway) config, [kanshi](https://github.com/emersion/kanshi/) config or [wlr-randr](https://github.com/emersion/wlr-randr) script.
