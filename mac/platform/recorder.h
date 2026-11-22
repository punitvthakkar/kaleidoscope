// Films: hardware HEVC Main10 in HLG (BT.2020), in an ordinary MP4 with the index
// first, with optional AAC stereo. One recording at a time. Memory is bounded by a
// fixed pool of frame buffers; frames that find no room are dropped at the source.
#pragma once
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>

enum RecorderError : NSInteger {
  RecorderErrorBusy = 1,         // a recording is already running
  RecorderErrorSize,             // picture too small
  RecorderErrorNoHardware,       // no hardware HEVC Main10 encoder for this configuration
  RecorderErrorSetup,            // Metal, encoder, or writer could not be set up (see NSUnderlyingErrorKey / status)
  RecorderErrorEncode,           // the encoder failed mid-recording
  RecorderErrorWrite,            // the file writer failed mid-recording
  RecorderErrorNoFrames,         // stopped before any frame was written
};
extern NSString* const RecorderErrorDomain;

// width/height are the renderer's picture size; odd sizes lose their last column/row.
// Host times are mach_absolute_time() ticks. onFailure (may be nil) is called once, on
// the main queue, if the recording fails while running; stop must still be called.
bool recorder_start(id<MTLDevice> device, int width, int height, NSURL* url,
                    bool hasAudio, double sampleRate,
                    void (^onFailure)(NSError* error), NSError** error);

// Render thread, once per displayed frame, before `cmd` is committed. `frame` is
// RGBA16Float extended linear sRGB (1.0 = SDR white), read by GPU work encoded into
// `cmd`, so it is ordered after the renderer's own work on the same queue. Frames are
// taken on a 60 fps grid by `hostTime` (the frame's display time). Never blocks.
// The conversion runs in a command buffer of its own, queued right after `cmd`.
void recorder_append(id<MTLCommandBuffer> cmd, id<MTLTexture> frame, uint64_t hostTime);
// Render thread: whether a frame shown at hostTime would be kept (it lands on a new
// slot of the 60 fps grid), so frames that would not can skip the capture work.
bool recorder_wants(uint64_t hostTime);

// Audio thread. Float PCM, one pointer per channel (right may be null for mono);
// hostTime is when the first frame is heard. Copies into a fixed ring; never allocates.
void recorder_append_audio(const float* left, const float* right, int frames, uint64_t hostTime);

// Finishes the film. `done` is called once on the main queue; url is nil on error.
void recorder_stop(void (^done)(NSURL* url, double seconds, int64_t droppedFrames, NSError* error));

bool recorder_active();

#ifdef FX_TEST   // test builds only (test_recorder.mm)
struct RecorderStats {
  int width, height;             // encoded size
  double bitsPerSecond;
  bool hardware;                 // the encoder reported hardware acceleration
  int64_t framesWritten, framesDropped, audioFramesDropped;
};
RecorderStats recorder_stats();

// Test only: delay every frame on its way into the encoder, to simulate a slow consumer.
void recorder_test_slow_consumer(int milliseconds);
#endif
