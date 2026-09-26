// The port's own icon, for the window. Original art: see tools/gen_icon.py,
// which draws it and generates src/icon_pixels.cpp. The .exe's icon comes
// from the same drawing, via packaging/windows/tubes-port.ico.

#pragma once

namespace tubes {

extern const int kIconSize;                 // width and height, in pixels
extern const unsigned char kIconRgba[];     // straight RGBA, rows top to bottom

}  // namespace tubes
