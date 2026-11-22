#pragma once
// The maths places (gen fractal3d): c and the 4D slice each frame, and the surface flight over
// the compact shapes (the Julia sets, the Mandelbulb, the Mandelbox). Their camera skims the
// surface, never enters it, and sinks closer as the zoom grows. Shared by the engine and the
// renderer test's stand-in engine.
#include "state.h"

// The surface below (eased), the camera's radius, which way it faces, and the ground scale
// (height over the surface's radius: an angle of flight that moves the ground one height).
struct RmCam { double rs = 0, rc = 0, head = 1, gs = 0.1; };

bool rm_surface(int shape);   // flown over (true) or flown through
// Fills f.rmQ, f.rmSlice, f.rmSurf and (when flown over) f.rmCam. yaw and pitch place the
// camera over the shape; yawV is the flight's speed, which way it faces follows it.
void rm_frame(int shape, double rmT, double morph, double detail, double slice0,
              double yaw, double pitch, double zoom, double yawV, double dt, RmCam& cam, Frame& f);
