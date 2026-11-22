// Camera and hand tracking (web: Inputs, the webcam and hand-tracking sections).
// The camera runs only while camera light or hands is on. Hands come from Vision,
// converted to MediaPipe HandLandmarker conventions (see HandObs in core/state.h).
#pragma once
#include <CoreVideo/CoreVideo.h>
#include <functional>
#include <string>
#include <vector>
#include "../core/state.h"

enum class CameraError { PermissionDenied, NoCamera, Failed };

struct CameraCallbacks {
  // On the tracking queue. count is 0 when no hand is seen (and once when hands
  // turn off). t is host-clock seconds (CACurrentMediaTime) of the camera frame.
  std::function<void(const HandObs* hands, int count, double t)> hands;
  // On the main queue.
  std::function<void(bool cameraRunning, bool handsReady)> state;
  std::function<void(CameraError)> error;
  // On the main queue: a camera was connected or removed (see camera_devices).
  std::function<void()> devices;
};

// Every camera that can be used now, built-in first. Any thread.
struct CameraDevice { std::string id, name; };
std::vector<CameraDevice> camera_devices();

struct Camera;
Camera* camera_create(CameraCallbacks cb);
void camera_destroy(Camera* c);

// Camera light wants 1280x720 (frames reach camera_take_frame); hands alone use 640x480.
void camera_set_light(Camera* c, bool on);
void camera_set_hands(Camera* c, bool on);
void camera_set_two_hands(Camera* c, bool on);
// Which camera to use, by CameraDevice::id; "" or an id that is not connected means
// the system's default. Changes over at once if the camera is running.
void camera_set_device(Camera* c, const char* id);
// The engine says a grip (pinch holding the glass) is held: keeps the 30 Hz tier.
void camera_set_grip_held(Camera* c, bool held);

// Render thread (web: takeFrame). Returns the newest frame, +1 retained, if one has
// arrived since the last call and camera light is on; otherwise NULL. BGRA,
// IOSurface-backed, Metal-compatible, unmirrored. The caller releases it.
CVPixelBufferRef camera_take_frame(Camera* c);

// Exposed for tests.
#ifdef __OBJC__
#import <Vision/Vision.h>
namespace camera_detail {
// Vision's joint name for MediaPipe landmark index i (0..20).
VNHumanHandPoseObservationJointName joint_name(int i);
// MediaPipe labels handedness as if the image were mirrored; Vision reports the hand
// actually in the image. On an unmirrored frame, MediaPipe's "Right" is Vision's left.
bool mediapipe_right(VNChirality c);

struct Joint { float x, y, c; };                       // Vision: normalised, origin bottom left
struct HandMemory { float x[21], y[21]; double t = -1e9; };
constexpr float kMinJointConfidence = 0.3f;
// j is in MediaPipe order. Returns false if too little of the hand is visible.
bool to_hand_obs(const Joint j[21], VNChirality chir, float score, double t,
                 HandMemory& mem, HandObs& out);
}
#endif
