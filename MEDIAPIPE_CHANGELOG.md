# MediaPipe Tasks Web: changelog from 0.10.14 to 1.2.3

Compiled 2026-11-15 for the Kaleidoscope app, which moved `@mediapipe/tasks-vision` from 0.10.14 (npm 2024-05-10) to 1.2.3 (npm 2026-11-11).

## Contents

- [Where this came from, and gaps](#where-this-came-from-and-gaps)
- [Release sections (newest first)](#release-sections-newest-first)
  - [v1.2.3](#v123) - GitHub 2026-11-11, npm 2026-11-11
  - [v1.2.2](#v122) - GitHub 2026-11-03, npm 2026-11-03
  - [v1.2.0](#v120) - GitHub 2026-10-28, npm 2026-10-27
  - [v1.1.1](#v111) - GitHub 2026-10-14, npm 2026-10-14
  - [v1.1.0](#v110) - GitHub 2026-10-07, npm 2026-10-06
  - [v1.0.0](#v100) - GitHub 2026-07-28, npm 2026-07-28
  - [v0.10.35](#v01035) - GitHub 2026-04-28, npm 2026-04-27
  - [v0.10.33](#v01033) - GitHub 2026-03-23, npm 2026-03-19
  - [v0.10.32](#v01032) - GitHub 2026-01-30, npm 2026-01-22
  - [v0.10.26](#v01026) - GitHub 2025-07-10, no npm package of that version
  - [v0.10.25](#v01025) - GitHub 2025-07-10, no npm package of that version
  - [v0.10.24](#v01024) - GitHub 2025-05-20, no npm package of that version
  - [v0.10.22](#v01022) - GitHub 2025-03-17, no npm package of that version
  - [v0.10.21](#v01021) - GitHub 2025-02-07, npm 2025-02-06
  - [v0.10.20](#v01020) - GitHub 2024-12-18, npm 2024-12-13
  - [v0.10.18](#v01018) - GitHub 2024-11-07, npm 2024-11-04
  - [v0.10.15](#v01015) - GitHub 2024-08-30, npm 2024-08-29
- [What this unlocks for the kaleidoscope](#what-this-unlocks-for-the-kaleidoscope)

## Where this came from, and gaps

Release notes are the GitHub releases of `google-ai-edge/mediapipe` (fetched with `gh api repos/google-ai-edge/mediapipe/releases --paginate`), taking every release published after v0.10.14 up to and including v1.2.3: 17 releases. Versions and dates were cross-checked against the npm registry (`registry.npmjs.org/@mediapipe/tasks-vision`). The notes are repository-wide (Android, iOS, Python, C++ framework, web all mixed). I removed the `Android`, `iOS`, `Python` and `Model Maker` sub-sections of the "MediaPipe Tasks update" part as platform noise and recorded the item counts removed in each section; the Bazel/framework lists, `Javascript` sub-sections and dependency notes are verbatim, including many lines that are native-only and irrelevant to the web build. The list is not a statement of what is in the npm package: the notes are not split per package.

Gaps and mismatches (nothing below was invented or filled in):

| npm version (date) | GitHub release? | Note |
|---|---|---|
| 0.10.16 (2024-09-19) | none | No release and no tag. The version numbers 0.10.16/0.10.17 only appear as "bump version" lines inside the v0.10.18 notes, so any change between v0.10.15 and v0.10.18 is described only there. |
| 0.10.17 (2024-10-03) | none | As above. |
| 0.10.19 (2024-12-11) | none | Only appears as a "bump version" line inside v0.10.20 notes. |
| 0.10.34 (2026-03-23) | none | No release and no tag `v0.10.34`. Its changes fall between v0.10.33 and v0.10.35 notes. |
| 1.0.1 (2026-07-31) | none | No release and no tag. Not described anywhere; the v1.0.0 notes (07-28) and v1.1.0 notes (10-07) bracket it. |
| 1.2.1 | none | Not on npm either. v1.2.2 notes contain a line "Bump MediaPipe version to 1.2.1". |
| none | v0.10.22, v0.10.24, v0.10.25, v0.10.26 | GitHub releases exist (Android/Maven/PyPI era) but the npm package `@mediapipe/tasks-vision` has no versions 0.10.22 to 0.10.31; npm jumps from 0.10.21 (2025-02-06) to 0.10.32 (2026-01-22). The web changes in those notes presumably shipped in 0.10.32, but that is an inference. Tag v0.10.23 exists without a release. |

I did not fall back to commit logs: the missing versions have no tags to compare (0.10.34, 1.0.1, 1.2.1), and no commit summaries are included anywhere in this file. Publish dates differ between GitHub and npm by 0 to 8 days (npm is usually first; for v0.10.33 npm preceded the GitHub release by 4 days; for v0.10.32, 8 days); both are shown per release. The release body for a tag that is "GitHub only" says nothing about whether a web package shipped.

Also checked directly (not from release notes): the published 0.10.14 and 1.2.3 package file listings (jsDelivr API), the `vision.d.ts` of both versions (diffed), and the minified `vision_bundle.cjs` and `wasm/vision_wasm_internal.js` of both. Those checks are cited in the analysis below.

## Release sections (newest first)

## v1.2.3

- Version: v1.2.3
- GitHub release published: 2026-11-11T23:34:01Z
- npm `@mediapipe/tasks-vision@1.2.3` published: 2026-11-11T22:49:09.236Z
- Source: https://github.com/google-ai-edge/mediapipe/releases/tag/v1.2.3
- Trimmed: removed platform-only sub-sections (Android: 1 items, iOS: 26 items, Python: 1 items); everything else is verbatim.

### Bazel changes

- Migrate `mediapipe::NoDestructor` to `absl::NoDestructor` (`absl/base/no_destructor.h`) in `mediapipe/framework` and remove obsolete `mediapipe/framework/deps/no_destructor.h`.
- Respect TensorBufferRequirements alignment in InferenceRunnerLiteRt
- Resolve file-path media parts in SemanticRetriever::Retrieve before embedding.
- [MediaPipe] Fix tensor view destruction deadlock on LiteRT run error.
- Register only the CPU accelerator in LiteRT environments for CPU-only runners.
- Add NPU dispatch library directory to C MpBaseOptions.
- Forward BaseOptions app telemetry in C++ tasks and Swift wrappers.
- Unify util/endian.h using absl/numeric/bits.h and remove deps/endian.h
- Format C++ code and fix includes in framework
- Format C++ code and fix includes in gpu
- Bump MediaPipe version to 1.2.3
- Support string input tensors in the MediaPipe LiteRT inference runner.
- Update Swift Package Manager files for the 1.2.3 iOS release.
- Added  C API classifier options conversion to MPPClassifierOptions+Helpers

### MediaPipe Tasks update
This section should highlight the changes that are done specifically for any platform and don't propagate to
other platforms.

#### Javascript
- Remove on-disk cache options from the web audio LiteRT-LM tasks.
- Fix DecisionMaker Web SDK APIs, boolean scoring, and native in-memory CPU loading.
- Support incremental TextToSpeech stream input on the web.

### MediaPipe Dependencies
- Explicitly include direct headers and BUILD dependencies.
- Update MediaPipe Tasks Web WASM files.


## v1.2.2

- Version: v1.2.2
- GitHub release published: 2026-11-03T22:08:41Z
- npm `@mediapipe/tasks-vision@1.2.2` published: 2026-11-03T18:00:14.139Z
- Source: https://github.com/google-ai-edge/mediapipe/releases/tag/v1.2.2
- Trimmed: removed platform-only sub-sections (iOS: 7 items, Python: 1 items); everything else is verbatim.

### Bazel changes

- Fix the tensor shape for column-major matrices in ConvertMatrixToTensorOnCpu.
- Use `'anonymous'` instead of `'Anonymous'` for `HTMLImageElement.crossOrigin`.
- Replace status_macros.h includes with absl/status/status_macros.h in mediapipe/gpu
- Replace status_macros.h includes with absl/status/status_macros.h in mediapipe/tasks/vision and benchmark
- Alias mediapipe::StatusBuilder and mediapipe::source_location to Abseil equivalents
- Migrate mediapipe::StatusBuilder callers to absl::StatusBuilder
- Migrate DRISHTI_CHECK_OK macros to ABSL_CHECK_OK
- Add C++ and C prerequisite fixes for audio and classifier options.
- Migrate statusor.h and StatusOr<T> to absl::StatusOr<T>
- Clarify in the `HandLandmarkerResult` and `GestureRecognizerResult` documentation across C, C++, iOS, Java, Python, and Web that `Left` and `Right` handedness correspond to the subject's hands rather than the viewer's, and that passing a horizontally mirrored image inverts these outputs.
- Migrate canonical_errors.h to absl/status/status.h
- Modernize ret_check.h to use absl::StatusBuilder and absl::SourceLocation
- Bump MediaPipe version to 1.2.1.
- Add support for configuring Qualcomm HTP PD session
- Migrate RETURN_IF_ERROR and ASSIGN_OR_RETURN to ABSL_* in framework
- Migrate RETURN_IF_ERROR and ASSIGN_OR_RETURN to ABSL_* in gpu
- Migrate RETURN_IF_ERROR and ASSIGN_OR_RETURN to ABSL_* in calculators/tensor
- Migrate RETURN_IF_ERROR and ASSIGN_OR_RETURN to ABSL_* in calculators
- Migrate RETURN_IF_ERROR and ASSIGN_OR_RETURN to ABSL_* in tasks
- Migrate RETURN_IF_ERROR and ASSIGN_OR_RETURN to ABSL_* in util, modules, and examples
- Migrate RETURN_IF_ERROR and ASSIGN_OR_RETURN to ABSL_* in tasks/cc/vision and benchmark
- Add MpBaseOptionsFree to C API base_options.
- Attach the model's output quantization to LiteRT inference runner outputs.
- Add `TaskRunnerOptions::memory_manager` to register a shared `MemoryManager` as `kMemoryManagerService` on the task's `CalculatorGraph` before initialization.
- Bump MediaPipe to 1.2.2
- Release LiteRT CPU output views early unless the output is AHWB-backed.
- Update Swift Package Manager files for the 1.2.2 iOS release.

### MediaPipe Tasks update
This section should highlight the changes that are done specifically for any platform and don't propagate to
other platforms.

#### Javascript
- Remove non-SIMD WebAssembly builds from the MediaPipe Tasks Web SDK.


## v1.2.0

- Version: v1.2.0
- GitHub release published: 2026-10-28T16:05:44Z
- npm `@mediapipe/tasks-vision@1.2.0` published: 2026-10-27T22:54:54.971Z
- Source: https://github.com/google-ai-edge/mediapipe/releases/tag/v1.2.0
- Trimmed: removed platform-only sub-sections (Android: 3 items, iOS: 2 items); everything else is verbatim.

### Bazel changes

- Bump MediaPipe version to 1.2.0
- Support Web Worker platform detection and Flutter Web telemetry overrides.
- Rollback due to breakage in gmscore.build.
- Fix GPU memory leaks when releasing AHWB-backed SSBOs and pooled textures.
- Fix nullptr exception in ImageToTensorCalculator
- Add support for F16 formats in ImageFrame
- Fix MediaPipe Tasks telemetry fields and audio_classifier.h license.
- Prevent possible lock-order-inversion deadlocks when locking input tensors in InferenceRunnerLiteRt
- Expose the remaining MlDriftDelegateOptions in InferenceCalculatorOptions
- Run face blendshapes on CPU when using the GPU delegate.
- Optimize Parakeet ASR streaming latency and decoder/runtime performance.
- Replace status_macros.h includes with absl/status/status_macros.h in mediapipe/framework
- Replace status_macros.h includes with absl/status/status_macros.h in mediapipe/calculators
- Replace status_macros.h includes with absl/status/status_macros.h in remaining mediapipe tasks, modules, and utilities
- Upgrade OpenCV to 4.10
- Enable F16 cv::Mat support in OpenCV conversions. OpenCV 4 was enabled in a previous CL.
- Update SPM files for 1.2.0

### MediaPipe Tasks update
This section should highlight the changes that are done specifically for any platform and don't propagate to
other platforms.

### MediaPipe Dependencies

- Update WASM files for 1.2.0 release
- Update genai wasm


## v1.1.1

- Version: v1.1.1
- GitHub release published: 2026-10-14T21:20:25Z
- npm `@mediapipe/tasks-vision@1.1.1` published: 2026-10-14T20:48:46.273Z
- Source: https://github.com/google-ai-edge/mediapipe/releases/tag/v1.1.1
- Trimmed: removed platform-only sub-sections (Android: 1 items); everything else is verbatim.

### Bazel changes

- Bump MediaPipe version to 1.1.1
- Add per-domain C libraries for Flutter SDK
- Fix GPU interactive segmentation on macOS.
- Add Flutter and Flutter Web host environments to MediaPipe Tasks C API.
- Add C API for intercepting Abseil logs in MediaPipe Tasks.
- mediapipe: Migrate to use new LiteRT GetOptions<T>() API
- Migrate remaining NPU option callers to GetOptions<T>()
- Update WASM and SPM files for 1.1..1 release

### MediaPipe Tasks update
This section should highlight the changes that are done specifically for any platform and don't propagate to
other platforms.

#### Javascript
- Update Retrieval SDK's code snippets

### MediaPipe Dependencies
- Upgrade LiteRT dependency to v2.3.0 in MediaPipe WORKSPACE


## v1.1.0

- Version: v1.1.0
- GitHub release published: 2026-10-07T00:47:41Z
- npm `@mediapipe/tasks-vision@1.1.0` published: 2026-10-06T17:55:06.100Z
- Source: https://github.com/google-ai-edge/mediapipe/releases/tag/v1.1.0
- Trimmed: removed platform-only sub-sections (Android: 11 items, iOS: 8 items, Python: 5 items); everything else is verbatim.

### Build and Bazel changes
- Remove the dependency on internal Bazel Apple rules `resources.bzl` in `mediapipe/metal.bzl`, using the metal rule provided by Bazel Apple rules.
- Add Python 3.13 and 3.14 support to the MediaPipe Bazel build.
- Upgrade LiteRT dependency to v2.2.0 in MediaPipe `WORKSPACE`.
- Move up declaration of `kissfft` in build configuration.
- Fix `xla llvm` download URL by using `mediapipe-assets` mirror.
- Fix Windows build.
- Remove obsolete `unistd.h` header imports.
- Configure MediaPipe for vendor/no-JNI Android builds.

### Framework and core calculator improvements
- Bump MediaPipe version to 1.1.0.
- Add MediaPipe Universal Embedder, Semantic Retriever, and LiteRT-LM integration across C++, C, Python, and Android/Maven APIs.
- MediaPipe Decision: Add EmbeddingGemma 1 & 2 decision backend with multimodal state and option support.
- Implement core `TaskPart` and `EmbeddingProvider` interface.
- Refactor `TextPart`, `AudioPart`, and `ImagePart` into `tasks.core` and unify duplicates.
- Refactor `TextEmbedder` and `ImageEmbedder` to use executor abstractions (`TextEmbedderExecutor`, `ImageEmbedderExecutor`).
- Add `HasFile`, `GetBundleManifest`, and `GetBundleId` helpers to `ModelAssetBundleResources`.
- Add `noexcept` to move ctor/operator and add `swap` to `Packet`.
- Add method to create an empty packet in Java API.
- Use `ABSL_ASSIGN_OR_RETURN` and `ABSL_RETURN_IF_ERROR` in MediaPipe.
- Rename `GeckoPreprocessorCalculator` to `SentencePiecePreprocessorCalculator`.
- Add option to ignore missing `encoded_media_bytes` in `UnpackMediaSequenceCalculator`.
- Add LiteRT-LM based C++ Audio Embedder.
- C API for MediaPipe AudioEmbedder.
- Select the MediaPipe OpenGL device by Vulkan device UUID.
- Replace LiteRT delegate with `use_litert` option in MediaPipe `BaseOptions`.
- Expose LiteRT and NPU delegate options to MediaPipe Tasks.
- Only use public LiteRT API in LiteRT inference calculator.
- Support passing `LiteRtSystemHandles` to Environment in LiteRT Inference Calculator.
- Add support for specifying a signature key for LiteRT inference.
- Retain `MTLCommandQueue` directly instead of `DrishtiMetalHelper` in `InferenceRunnerLiteRt`.
- Use runtime layout for rank-0 LiteRT model outputs in `InferenceRunnerLiteRt`.
- Check GPU service availability in `TensorsToDetectionsCalculator`.
- Improve MediaPipe Metal GPU delegate error messages for unsupported configurations.
- Allow size mismatched tensors for GPU execution.
- Fix race condition in `GpuResources`.
- Fix deprecated `CreateSourceTexture` usage in desktop GPU example.

### MediaPipe Tasks update
This section highlights the changes that are done specifically for each platform and don't propagate to other platforms.

#### Javascript
- Add retrieval to MediaPipe Tasks for Web (`UniversalEmbedder`, `SemanticRetriever`).
- Export public APIs for Web `SemanticRetriever`.
- Make the `DecisionMaker` web API usable from the minified `decision_js` bundle.
- Refactor task name discovery in MediaPipe Web Tasks.
- Expose per-landmark presence scores on the Web Tasks API.
- Enable GPU usage for `UniversalEmbedder` on Web.
- Prevent overlapping WASM embedding calls.
- Disable LiteRT-LM extensive logging for `UniversalEmbedder` on Web.
- Add stats logging to Web Tasks.
- Add unit tests for `ImageFrame` and `YUVImage` conversions.
- Add missing `await` calls to `UniversalEmbedder` example in web retrieval documentation.

### Bug fixes
- Fix `HolisticLandmarker` crash when face detections are missing.
- Fix `HolisticLandmarker` `Any` Builder return type and empty packet `MediaPipeExceptions`.
- Re-add `ImageEmbedder.close()` method.
- Fix `numpy_view()` SIGABRT on non-contiguous `float32` masks.
- Fix wrist connection in `HAND_PALM_CONNECTIONS`.
- Fix landmark drift in `LandmarksSmoothingCalculator` (OneEuroFilter) when stream resolution dynamically changes between preview and still capture.
- Correct parameter names in core calculators.
- Fix WAV decoder calculations in `audio_decoder_calculator_test`.

### MediaPipe Dependencies
- Upgrade LiteRT dependency to v2.2.0 in `WORKSPACE`.
- Update WASM asset binaries for 1.1.0 release.


## v1.0.0

- Version: v1.0.0
- GitHub release published: 2026-07-28T22:13:25Z
- npm `@mediapipe/tasks-vision@1.0.0` published: 2026-07-28T01:13:17.090Z
- Source: https://github.com/google-ai-edge/mediapipe/releases/tag/v1.0.0
- Trimmed: removed platform-only sub-sections (Android: 5 items, iOS: 7 items, Python: 6 items); everything else is verbatim.

### Build changes
- Remove MacOS prebuilts
- Replace ITUIOSTestUtil with ITUTestRunEnvironment in iOS tests.

### Bazel changes
- Migrate Apple rules dependencies to MODULE.bazel.
### Framework and core calculator improvements
- Add Gecko support to MediaPipe Text Embedder Task
- Store wgpu::Instance in WebGpuService.
- Supporting builds with DRISHTI_DISABLE_GL_COMPUTE define.
- Bump MediaPipe version to 0.10.36.
- - Abseil (com_google_absl): Updated from (2023-10-18) to (2025-01-14).
- Add Gecko support to TextEmbedder C API.
- Use standardized enum constants for Running Mode
- Report Mat type in debug logging.
- Add AddAudioHeaderCalculator
- Remove run_llm_inference.sh
- Add webgpu::Instance to WebGpuDeviceAttachmentManager.
- Fix MediaPipe nightly build failure and cleanup Bzlmod warnings.
- Fix null pointer access in Tensor::GetOpenGlTexture2dReadView() for AHWB backed Tensor.
- Adds SpectrogramFloatCalculator and InverseSpectrogramFloatCalculator to support float precision.
- Add support for Embedding Gemma models in TextEmbedder.
- Make wgpu::Instance a member of WebGpuAsyncFuture.
- Enforce namespacing conventions for MediaPipe Tasks C and Python APIs.
- Add methods to create and access Matrix packets using Direct ByteBuffers.
- Ensure AhwbGpuResources destructor cleans up OpenGL/EGL resources and forces AHWB usage completion.
- Update MediaPipe Python build Dockerfiles.
- Update base options and host environment.
- Introducing extra base options and corresponding conversion/get functions.
- Improve error message for option_value problems
- Support models with empty tags
- Add empty image checks to ScaleImageCalculator.
- Add gpu_postprocessing option for TensorsToSegmentationCalculator.
- Add text chunking utility
- Expand LiteRtService to support system runtime on Android.
- Add `vImageExtractChannelAndConvertToFloat` function.
- Compile with OpenCV 5's features
- Add documentation for LiteRT Inference Calculator accelerators
- Fix data race accessing GlContext::profiling_helper_
- Add a "prefer_ahwb" flag to MemoryManager
- Add Builder v2 and v3 APIs to change the node name
- Add verbose debug logging for buffer pooling
- Updates the manylinux Dockerfile and related build scripts to fix the
- Re-format GraphRunner
- Add EXIF orientation support to MpImageCreateFromFile and the Python Task API

### MediaPipe Tasks update
This section should highlight the changes that are done specifically for any platform and don't propagate to
other platforms.

#### Javascript
- Add Gecko support to MP Text Embedder for Web
- Add a Module.BAZEL to MediaPipe and migrate JS depdendencies to Aspect JS
- Add dummy logging framework
- Add MediaPipe Tasks Privacy Notice
- Add IIFE bundles for web task modules.
- Export InteractiveSegmenter methods for public visibility
- Allow MP Web LLM Inference to load .litertlm files containing web-formatted LLMs
- MP Web LLM Inference: add new experimental 'disableRewinding' flag for testing long-context support
- Cache running mode in VisionTaskRunner.

### MediaPipe Dependencies
- Do not load text files from GCS


## v0.10.35

- Version: v0.10.35
- GitHub release published: 2026-04-28T17:55:39Z
- npm `@mediapipe/tasks-vision@0.10.35` published: 2026-04-27T18:20:29.895Z
- Source: https://github.com/google-ai-edge/mediapipe/releases/tag/v0.10.35
- Trimmed: removed platform-only sub-sections (Android: 2 items, iOS: 1 items, Python: 3 items); everything else is verbatim.

### Framework and core calculator improvements
- Add histogram information to Tensor::DebugString.
- Bump MediaPipe version to 0.10.35.
- Add missing GL memory barrier in TensorsToSegmentationGlBufferConverter.
- Migrate FromImageCalculator to MediaPipe API3 and add test.
- Add api3::Packet::Share function.
- Remove util/analytics references from GitHub build
- Migrate ToImageCalculator to MediaPipe API3 and add tests.
- Fix -Wthread-safety-analysis warning.
- Allow MP Task files to be use in Vite's workers
- Add save-png-by-path test util function.
- Feat: Add configurable policy for handling empty landmarks in smoothing calculators
- #mediapipe Make GPU service optional in ImageToTensorCalculator for iOS.
- Add Host Platform Web and Host System iOS/Android to logging enums

### MediaPipe Tasks update
This section should highlight the changes that are done specifically for any platform and don't propagate to
other platforms.

#### Javascript
- Remove references to "subgroups-f16"
- Fix broken exports statement in package.json
- Update MP Tasks GenAI README

### MediaPipe Dependencies
- Update Wasm file hashes and URLs in wasm_files.bzl


## v0.10.33

- Version: v0.10.33
- GitHub release published: 2026-03-23T20:55:04Z
- npm `@mediapipe/tasks-vision@0.10.33` published: 2026-03-19T15:37:02.437Z
- Source: https://github.com/google-ai-edge/mediapipe/releases/tag/v0.10.33
- Trimmed: removed platform-only sub-sections (Python: 5 items); everything else is verbatim.

### Build changes

- [mediapipe] mark mediapipe/gpu:gl_context incompatible on Windows
- [mediapipe] automatically disable gpu (OpenGL) when building for Windows
- [mediapipe] update visibility of gpu:disable_gpu_flag


### Framework and core calculator improvements
- Add Nearest Neighbor interpolation to WarpAffineCalculator.
- Add a Tensor DebugString() function that formats the tensor numpy-style
- Add a Tensor test util
- Create a C API for Holistic Landmarker
- Remove the CPU-only MediaPipe LLM Inference Engine
- Add an AddMultiStreamCallback version that takes a packet map
- BUILD rule cleanup
- API3 subgraph support
- Adds LogImage for mediapipe::Image
- Add support for FULL_RANGE face detection task
- Ads optional RESET input stream to SpectrogramCalculator
- Simplify TaskRunner initialization by adding an options object
- Plumb the host platform and version for all MP Tasks users.
- MediaPipe: Add ROI validation in ImageToTensorOpenCvConverter

### MediaPipe Tasks update
This section should highlight the changes that are done specifically for any platform and don't propagate to
other platforms.


#### Javascript
- Add tests for full-range face detector model.


## v0.10.32

- Version: v0.10.32
- GitHub release published: 2026-01-30T19:49:50Z
- npm `@mediapipe/tasks-vision@0.10.32` published: 2026-01-22T23:28:33.490Z
- Source: https://github.com/google-ai-edge/mediapipe/releases/tag/v0.10.32
- Trimmed: removed platform-only sub-sections (Android: 8 items, iOS: 5 items, Python: 43 items); everything else is verbatim.

### Build changes

- Enables ml drift metal delegate as inference calculator backend.
- [mediapipe] support armv7 (32 bit in mediapipe tasks)
- Do not assume canvas is BGRA in RenderToWebGpuCanvas.
- Fix sampling logic in ImageToTensorConverterWebGpu.
- Migrate GlShaderCalculator to API3.
- Migrate gl_shader_calculator_test to use API3 builder.

### Bazel changes

- [mediapipe] verion bump to 0.10.27
- Dawn has completed these changes, so the old paths are no longer used.
- Integrate tiny Juno inpainting graph into GenAiProcessor
- Readme for API3
- Add Resources::ResolveId to enable placeholder resource ids usage.
- Web LLM: a few more small edits for Gemma3n
- Include headers from global namespace
- Add comment to Eigen version in WORKSPACE to remind about synchronization with TensorFlow's Eigen dependency
- Migrating VisibilityCopyCalculator to API3.
- Update from Bazel v6.5.0 to v7.4.1, Protobuf v3.19.1 to v5.28.3. Other packages also update the version within WORKSPACE.
- Fix for weight cache on Windows.
- Create Selfie Segmentation Demo App for LiteRT NPU.
- Add Any support for API3
- Adding AudioBuffer support to web LLM Inference API to handle more audio input types for MM models
- Provide API3 interface for PassThroughCalculator using newly added Any type.
- Migrate MergeCalculator to API3 and newly introduced Any type.
- pybind11 version and py_proto_library macro update.
- Add test for PacketResamplerCalculator with a very short video.
- Initial version of sync function runner for API3
- Fix function runner error reporting.
- [mediapipe] version bump
- Migrate CombinedPredictionCalculator to API3
- Clean up CombinedPredictionCalculator
- Currently, wrapping a TextureFrame in a media-pipe Packet assumes the texture is 8-bit RGBA. This patch allows specifying other texture formats to support common color formats like RGBA16F for HDR content.
- Support timestamp bound updates in function runner.
- Migrate TensorsToSegmentationCalculator to MediaPipe API3.
- Add OneOf support for API3.
- Provide ineference calculator API3 interface.
- Migrate LandmarksToMatrixCalculator to API3
- Update MediaPipe OSS to C++20.
- Add a flag to use `fp16` activations in tests.
- Migrate HandednessToMatrixCalculator to API3.
- Update `xnnpack` version.
- Use the new `xnn_reduce_mean_squared` reduction for the RMSNorm.
- Migrate ImageToTensorCalculator to API3.
- Consistently use MutexLock instead of manual locking/unlocking
- Add ImageProcessingOptions to FaceDetector C API
- Enable node names as compile time strings in OSS.
- Migrate API3 nodes to use compile time string names.
- Fall back to producer context in gpu_buffer.GetReadView<GlTextureView>
- Document api3 GetOrDie / VisitOrDie
- Update log for missing InferenceCalculatorXnnpack registration.
- Add NodeName for non-generic calculator context.
- Add `ImageProcessingOptions` support to FaceLandmarker C API.
- Migrate FaceLandmarker C API to use MediaPipe Image
- Update CombinedPredictionCalculator test to new Runner
- Fix comment about when things die.
- Migrate WebGpuShaderCalculator to MediaPipe API3.
- Proto changes for Tiny Gemma on ml_drift
- Enable API3 FunctionRunner for WEB
- Bump MediaPipe version to 0.10.29.
- Add CompareAndSaveImageOutputDynamic to compare to a dynamic golden instead of a file.
- Bump MediaPipe version to 0.10.30.
- Improve error message of graph validation, to include node calculator name
- Refactor Hand Landmarker C API to use new MP Image
- Add ExternalGlTextureSyncMode to require efficient synchronization.
- Add an option to get a Packet for API3 OneOf input.
- Migrate GpuBufferToImageFrameCalculator to API3.
- Add support to pass a single visitor in VisitOrDie for OneOf inputs.
- Add VisitAsPacketOrDie for OneOf inputs.
- Add MediaPipe Tasks C API for AudioClassifier.
- Ensure correct type of #api3 Packet.
- Support fractional frame rates in MediaPipe video processing.
- Add `ImageProcessingOptions` support to Object Detector C API.
- Add `ImageProcessingOptions` support to MediaPipe PoseLandmarker C API.
- Update object detector to apply ImageFrame C API
- Update pose landmarker to apply ImageFrame C API
- Migrate HandAssociationCalculator to MediaPipe API3.
- Adding ImageProcesingOptions to image_classifier C API.
- Adding ImageProcesingOptions to gesture_recognizer C API.
- Migrate GestureRecognizer (C API) to MpImagePtr
- Migrate ImageFrameToGpuBufferCalculator to API3
- Set default thread_num to  LiteRT::CPU delegate
- Qualify Packet and MakePacket while in mediapipe::api3 namespace to avoid future collisions with api3::Packet / api3::MakePacket
- Remove redundant empty parentheses from lambdas in MediaPipe API3.
- Migrate ImageClassifier (C API) to MpImagePtr.
- Update HandAssociationCalculatorTest to new Runner
- Add `ImageProcessingOptions` support to Image Segmenter C API
- Migrate TensorsToSegmentationCalculator to API3
- Extend lifetime of Image data when MpImage is constructed from an existing MediaPipe Image
- Allow GetData() calls for contiginuous images
- Migrate ImageSegmenter C API to MpImagePtr
- Add GetLabels() to the ImageSegmenter C API
- Generalize UnpackMediaSequenceCalculator's support for encoded media streams.
- Clean up unused variables
- Get rid of _with_options in favor of optional param (C & Python API)
- Migrate Python ImageSegmenter to C API
- Refactor Image Embedder C API to use MpImage
- Simplify FunctionRunner template types.
- Simplify setting options in `HandAssociationCalculatorTest`
- Update MediaPipe C API vision task result callbacks to use `MpStatus`.
- Offer attachments functionality from WebGPU service.
- Enable creation of WebGPU service from explicitly provided wgpu::Device.
- Remove unnecessary checks.
- Enables RGBA input with RGB output.
- Modify the Has{} generic function to check across the different value kinds of
- Refactor TextClassifier C API to use MpStatus.
- Update C API for TextEmbedder to use MpStatus
- Update C API for LanguageDetector to return MpStatus
- Add ImageProcessingOptions support to the MediaPipe ImageEmbedder C API.
- Add ImageProcessingOptions to InteractiveSegmenter C API and migrate to MpImage
- Fix counting pixels with different colors for image comparison tests
- Remove redundant `has_confidence_masks` field from `ImageSegmenterResult`.
- Migrate ImageClassifier C API to use MpStatus.
- Refactor Face Detector C API to use `MpStatus`.
- Update ImageSegmenter C API to return MP Status
- Add support for XNNPACK's SLOW_CONSISTENT_ARITHMETIC flag
- Update ImageEmbedder C API to return MP Status
- Update InteractiveSegmenter C API to return MP Status
- Get rid of _with_options in favor of optional param (C)
- Refactor Gesture Recognizer C API to use MpStatus.
- Update ObjectDetector C API to return MpStatus
- Update HandLandmarker C API to return MP Status
- Update PoseLandmarker C API to return MpStatus
- Fix libmediapipe.so compilation on Windows
- Remove no longer used Image types
- Add side packets support for FunctionRunner.
- Fix documentation.
- Update bot assignees in bot_config.yml.
- Added new R8 mode for GlShaderCalculator
- Add visibility declarations for Windows
- Centralize WebGPU header includes
- Web Solutions: patch for importScripts error with modules in workers
- Small cleanups in MP Task C++ segmentation graphs and ModelTaskGraph
- Update AudioClassifier to retain error messages
- Retain error messages in the Metadata API
- Retain error messages in Language Detector
- Update GestureRecognizer to retain error messages
- Update FaceLandmarker C API to use new naming and return type convention
- Retain error messages in TextEmbedder
- Retain error messages in HandLandmarker
- Retain error messages in ImageEmbedder
- Retain Error Messages in Object Detector
- Small cleanup of scheduler_queue
- Refactor ImageClassifier C API to return error messages.
- Allow empty Tensors in InferenceCalculator
- Update ImageSegmenter to retain error messages
- Retain error messages for PoseLandmarker
- Retain error messages in MpImage
- Allow packing of input streams into empty SequenceExample.
- Retain error messages in Interactive Segmenter
- Replace custom test macros with EXPECT_EQ/ASSERT_EQ
- Add MpErrorFree to avoid missing function on Windows
- Bump MP version to 0.10.31
- Add Kotlin support to MediaPipe OSS repo
- Prepare for functiongemma with MP web LLM API
- Add experimental mapSync support in GetTexture2dData.
- Fail with error at empty decoded image in OpenCVEncodedImageToImageFrameCalculator. Empty decoded image can happen if decoding fails.
- Support option dependencies in mediapipe proto rules
- Fix logging large one-dimensional vertical data
- Adds GPU output support for category masks (copy only, result listener zero-copy case is not addressed yet)
- Add wgpu::ExternalTexture support

### MediaPipe Tasks update
This section should highlight the changes that are done specifically for any platform and don't propagate to
other platforms.


#### Javascript
- Web LLM: basic .wav audio support for Gemma 3N
- Web LLM: Small fix to multimodal error message strings
- Add a test case to test empty packet inputs.
- Migrate tasks tests to use common image test util.
- Enables GraphRuntimeInfo feature for Emscripten / web targets
- Web LLM API: cancelProcessing() call for early-exiting during decoding. Will not work until next npm update.
- Fix attachments synchronization for non-WEB use cases.
- Fix for MPMask f32-->u8 conversions not always working consistently on all devices. See https://github.com/google-ai-edge/mediapipe/issues/5862.

### MediaPipe Dependencies

- Fix OpenCV dependency.
- Update WASM files for 0.10.32 release


## v0.10.26

- Version: v0.10.26
- GitHub release published: 2025-07-10T16:30:22Z
- npm `@mediapipe/tasks-vision@-` published: n/a (no such npm version)
- Source: https://github.com/google-ai-edge/mediapipe/releases/tag/v0.10.26
- Trimmed: nothing.

### 16kb Page Size Support

- All the latest Android packages from Google Maven are now supporting the Android 16kb page size.
- `0.10.26.1` includes also the support for ARM v7 CPUs (32-bit).

### Bazel changes

- mediapipe task version bump
- Introduce new variant of TFLiteModelLoader::LoadFromPath that allows to specify the mmap mode
- Add DefaultSidePacketCalculator unit test under calculators/core
- Add a test parameter to ignore pixels above diff limit
- - Add MaskOverlayCalculator unit test under calculators/image
- Web LLM: make GetSizeInTokens work for first vision-capable models
- Log invalid format in proto lite mode.
- Needed in order to update Dawn to match the standard webgpu.h, here:
- Migrate InverseMatrixCalculator to API3
- Migrate WarpAffineCalculator to #mediapipe-api3 + introduce GetGenericContext
- Migrate landmark_projection_calculator to API3
- Inference calculator refactoring.
- Adding int32-vector output to constant side-packet calculator
- Switch usages of `ShaderModuleWGSLDescriptor` to `ShaderSourceWGSL`
- [mediapipe] update to Android SDK and NDK 26 -> 28
- Migrate world_landmark_projection_calculator to API3
- Migrate landmarks_refinement_calculator to API3
- [mediapipe] upgrade docker image to use JDK 21
- Cleanup projection calculator from reliance on C++ variable scoping and add a new test case.
- Update std::once_flag/call_once to absl::once_flag/call_once in OSS version

### MediaPipe Tasks update
This section should highlight the changes that are done specifically for any platform and don't propagate to
other platforms.

- Add audio modality support to LLM Inference API.
- [mediapipe] update opencv dependency


## v0.10.25

- Version: v0.10.25
- GitHub release published: 2025-07-10T16:26:49Z
- npm `@mediapipe/tasks-vision@-` published: n/a (no such npm version)
- Source: https://github.com/google-ai-edge/mediapipe/releases/tag/v0.10.25
- Trimmed: nothing.

### Bazel changes

- Make ThreadPoolExecutorOptions callable from Java/Kotlin
- Add contract validator for API3
- API3 Extract reusable part of API2 graph builder.
- Adding license headers.
- Post release version bump
- API3 calculators should default to timestamp offset 0 (the same default as in API2) for consistency and it should be possible to unset the default.
- API3 graph, stream & side_packet
- Add SRQ config option into TransformerParams proto.
- Graph API2 API3 interop.
- Adding basic vision+text testing capabilities to web LLM Inference API

### MediaPipe Tasks update
This section should highlight the changes that are done specifically for any platform and don't propagate to
other platforms.

- Drops duplicated Android manifest file.
- Makes LlmTaskRunner internal.

#### Javascript
- Web LLM: Minor refactoring to allow more usage of newer LLM code paths


## v0.10.24

- Version: v0.10.24
- GitHub release published: 2025-05-20T19:23:00Z
- npm `@mediapipe/tasks-vision@-` published: n/a (no such npm version)
- Source: https://github.com/google-ai-edge/mediapipe/releases/tag/v0.10.24
- Trimmed: removed platform-only sub-sections (Android: 9 items, iOS: 2 items, Python: 9 items); everything else is verbatim.

### Build changes
- Add FdFinishedFunc util to mediapipe
- rename a config setting to BUILD_FOR_OSS
- #mediapipe #ios remove custom cpp version (rely on the common cpp version set at build time)
- Rely on the common cpp version set at build time.

### Framework and core calculator improvements 
- Update C++ Graph Builder to support source layers.
- Bump MP version for release 0.10.23.
- Add Back-Edge support in Graph builder.
- Add a destructor to WebGpuAsyncFuture that correctly frees any pending future.
- Add tools for logging Tensors, ImageFrames and cv::Mats
- Add a utility for creating a view of a Tensor into an OpenCV Mat
- Add WebGpuCreateRenderPipelineAsync utility.
- [mediapipe] update documentation mentioning python versions
- Bump MP version for release 0.10.24.
- Add support for `GemmaV2-2B` via XNNPACK.
- Remove obsolete checks that integer division rounds to zero.
- Inline SafeIntStrongIntValidator::SanityCheck function
- Debug logging: Fix and properly support logging RGBA images
- Fix modules/face_detection documentation.
- Add LogHalideBuffer variant for logging Halide buffers
- Add support for `GemmaV3-1B` models using XNNPACK.
- Correct documentation to reflect actual behavior.
- Fix KleidiAI repository URL.
- Removed usage of deprecated InitFromGraphWithTransforms.
- Dynamically quantize inputs only once before projecting to queries, keys, and values.
- add an enum option to spectrogram calculator to output frames with all channels instead of vector of matrices
- Fix GlBufferView (bug: incomplete move constructor)
- Don't recreate write views on the same internal-only-use tensor (which triggers error messages) and fix read/write view usages.
- Support loading PackWeightsCache from a file descriptor
- Update flag description to use correct name for input_token_limit
- Reduce logging frequency for some warnings.
- Allow header output for all resampling strategies.
- Fix failing build: `blaze --blazerc=/dev/null build //third_party/mediapipe/examples/ios/facedetectioncpu:FaceDetectionCpuApp.apple_binary --config=ios_arm64 --ios_minimum_os=12.0`
- Add std::vector<float> output support to ConstantSidePacketCalculator
- Avoid creating unused StatusRep objects on each CalculatorNode::ProcessNode call
- Avoid creating multiple status reps on each mediapipe::tool::StatusStop() call
- Add option to process timestamp bound for ImmediateMuxCalculator.

### MediaPipe Tasks update
This section should highlight the changes that are done specifically for any platform and don't propagate to
other platforms.

#### Javascript

- Remove artificial limits on maxBufferSize and maxStorageBufferBindingSize for LLM Inference on web.
- Use different parameters (topk, temperature) for gemma3
- Web LLM Inference: better error messaging for re-entry occurring from callback
- Add toggle for allowing the forcing of float32 precision for LLM Inference on web

### MediaPipe Dependencies
- Update WASM files for 0.10.22 release


## v0.10.22

- Version: v0.10.22
- GitHub release published: 2025-03-17T22:42:21Z
- npm `@mediapipe/tasks-vision@-` published: n/a (no such npm version)
- Source: https://github.com/google-ai-edge/mediapipe/releases/tag/v0.10.22
- Trimmed: removed platform-only sub-sections (Android: 11 items, iOS: 1 items); everything else is verbatim.

### Build changes
- [mediapipe] standardize import of androidx_annotation_annotation
- [mediapipe] standardize import of androidx_appcompact
- [mediapipe] standardize import of androidx_constraint_layout
- [mediapipe] standardize import of androidx_core
- [mediapipe] standardize import of androidx_legacy_legacy_support_v4
- [mediapipe] delete unused 3p android_library androidx_material
- [mediapipe] standardize import of androidx_recyclereview
- [mediapipe] standardize import of camerax
- Fix llm_engine_main build for DRISHTI_DISABLE_GPU=1

### Framework and core calculator improvements
- Updating Troubleshooting with VLOG info.
- Update tensors_to_image_calculator.cc
- Delegate memory-mapping the model file to the resource system
- Add static helpers to timestamp classes
- Remove use of designated initializers in tflite_model_loader.cc
- Add support for INT64 in `VectorIntToTensorCalculator`.
- Use renamed wgpu::ImageCopy* structures.
- [mediapipe] improve  mediapipe_java_proto_src_extractor
- Bump MP version for release 0.10.22.
- [mediapipe] improve maven artifact template
- Add two_tap_fir_filter_calculator and update com_google_audio_tools revision.
- Adds check to reject services with an empty shared_ptr
- Adds check to ensure input tensors match model tensor size & type
- Replace `MapName` with `StaticMap` in places where it's not important to use `MapName`
- Make DelayedReleaser an "attachement" of the GlContext instance.
- Utility functions that create RGB images for testing.
- Avoids the sharing of GL contexts between nested mediapipe graphs.
- Adds output stream stats to GraphRuntimeInfo
- Move ImageFrames while splitting a vector of ImageFrames.
- Add input stream to control zoom factor used in content_zooming_calculator.
- Introduces GPU synchronization when accessing GetOpenGlBufferReadViews from a different OpenGL context than was used for the GetOpenGlBufferWriteView.
- Adds documentation about graph runtime monitoring.
- Use wgpu::ShaderSourceWGSL instead of wgpu::ShaderModuleWGSLDescriptor.
- [mediapipe] restore mediapipe_aar.bzl
- Add CreateWgslShader utility.
- Update resource loading in WebGpuShaderCalculator to latest API.
- Added prompt templates for session in C API

### MediaPipe Tasks update
This section should highlight the changes that are done specifically for any platform and don't propagate to
other platforms.

#### Javascript
- Fix DrawingUtils constructor failing in Web Workers
- Change starting LoraModel ids from 0 to 1.
- Add a function to determine what type of model (handwritten, converted) a file is
- Fix tee not cancelling the parent stream when both children are cancelled
- Distinguish between '.bin' and '.task' in createFrom*
- Move streamToUint8Array from task runner lib to model loading utility lib, so the graph runner extensions would be able to utilize it.

### MediaPipe Dependencies
- Update WASM files for 0.10.21-rc.20250303 release
- Update WASM files for 0.10.22 release


## v0.10.21

- Version: v0.10.21
- GitHub release published: 2025-02-07T22:57:15Z
- npm `@mediapipe/tasks-vision@0.10.21` published: 2025-02-06T20:25:37.078Z
- Source: https://github.com/google-ai-edge/mediapipe/releases/tag/v0.10.21
- Trimmed: removed platform-only sub-sections (Android: 4 items, Model Maker changes: 4 items); everything else is verbatim.

### Framework and core calculator improvements

- Update tensors_to_image_calculator.cc
- Fix incorrect name in ValidateRequiredSidePacketTypes status message.
- Delegate memory-mapping the model file to the resource system
- Add documentation for GpuOrigin::DEFAULT
- Add multiclass nms options for object detector.
- Add static helpers to timestamp classes
- Add Dockerfiles to allow users to build their own wheels
- Remove std::aligned_storage.
- Nit: add details to "no implementation available" error message
- Remove use of designated initializers in tflite_model_loader.cc
- Add resample_time_series_calculator.

### MediaPipe Tasks update
This section should highlight the changes that are done specifically for any platform and don't propagate to
other platforms.

#### Javascript
- [Web LLM] Fix for duplicate timestamp issue that could occur when loading two LoRA models in immediate succession
- Return error code and file error message in C API for both PredictSync and PredictAsync
- Added isIdle function to check whether web LlmInference instance is ready for work.
- Make the parameters for generateResponse optional.


## v0.10.20

- Version: v0.10.20
- GitHub release published: 2024-12-18T22:17:22Z
- npm `@mediapipe/tasks-vision@0.10.20` published: 2024-12-13T21:31:29.608Z
- Source: https://github.com/google-ai-edge/mediapipe/releases/tag/v0.10.20
- Trimmed: removed platform-only sub-sections (Android: 4 items, iOS: 1 items, Python: 2 items, Model Maker changes: 1 items); everything else is verbatim.

### Build changes

- Add comments to explain how to configure OpenCV in the opencv_macos.BUILD file.
- Add libc++_shared.so to MediaPipe Android examples.
- Add linkstatic to OpenCV prebuilts

### Framework and core calculator improvements

- Fix ParseFromString() compilation issue in OSS
- All the dead links fixed
- Add troubleshooting tip for unsupported XNNPACK flags during build
- Add UniqueId::Dup.
- Updating the XNNPACK latest commit hash
- Format Workspace file
- Add EglSync wrapper.
- Update Bazel version to 6.5.0
- Update sync_wait to support UniqueFd.
- Fix GlContext includes
- Add EglSyncPoint/CreateEglSyncPoint.
- Bump MediaPipe version to 0.10.19.
- More perfetto tracking for EglSync.
- Patch for supporting WebGPU .deviceInfo during API migration.
- Log the Tensor multi-write error message only once.
- Enable GpuBufferStorageAhwb ASYNC usage for use case: AhwbView write -> GlTextureView read
- Add IsSignaled function (the previous SyncWait for checking status triggers unnecessary StrFormat)
- Add type information to error message when accessing an empty packet.
- Add SharedFD type
- Update SyncWait/IsSignaled to work with SharedFd.
- Enable SharedFd usage in EglSync
- Adding VLOG overrides - MediaPipe utilizes VLOG heavily, but it's not straightforward for how to enable this when running an Android app. VLOG overrides allow to relatively quickly enable VLOGs for various modules within MediaPipe.
- Updating Troubleshooting with VLOG info.
- Slice only the tokens which are needed for the next stage of the LLM pipeline.
- Adds DebugInputStreamHandler.
- Delete YUVImage copy and move operations
- Adds GetGraphRuntimeInfo methods which generates runtime debugging information about the state of InputStreams.
- Add a sample script to run LLM inference on Android via the MediaPipe LLM inference engine.
- Update bot_config.yml
- Add option to set max sequence size in PackMediaSequenceCalculator instead of having it hard coded.
- Update run_llm_inference.sh with recommended models.
- Allow to read the input frame rate from the header in the input side stream and to limit the frame rate.
- Add stream operator<< for TypeId
- Extract native-to-UTF8 path string conversion; add FormatLastError()
- Update comment in yuv_image.h
- Introduce `shadow_copy` parameter to `PathToResourceAsFile`
- Fix header includes after refactoring
- Migrate away from status builders
- Avoids the creation of two "default" GpuExecutor instances
- Adds and integrates GraphRuntimeInfoLogger into CalculatorGraph.
- nit: don't overwrite InitializeDefaultExecutor argument "use_application_thread"
- Add Name() access to the source names in api2.
- Add memory mapping and locking to file helpers
- Fix Windows build
- Fix Windows build, part 2
- Support memory mapping in resources.
- Bump MediaPipe version to 0.10.20.
- Enable log message output for messages larger than 4096 bytes.
- Add vision modality to the C API

### MediaPipe Tasks update
This section should highlight the changes that are done specifically for any platform and don't propagate to
other platforms.

#### Javascript

- Improve logging to allow users to understand 1) which InferenceCalculator backend is used (without extra VLOG flags) and 2) when a model is loaded (including its size).
- nits: Remove linter warnings, fix unused includes.

### MediaPipe Dependencies

- Update WASM files


## v0.10.18

- Version: v0.10.18
- GitHub release published: 2024-11-07T23:34:59Z
- npm `@mediapipe/tasks-vision@0.10.18` published: 2024-11-04T21:56:28.210Z
- Source: https://github.com/google-ai-edge/mediapipe/releases/tag/v0.10.18
- Trimmed: removed platform-only sub-sections (Android: 4 items, iOS: 31 items, Python: 6 items, Model Maker changes: 3 items); everything else is verbatim.

### Build changes

- Following open-sourcing webgpu with open-sourcing one of its dependencies `third_party/emscripten`
- Add pillow, pyyaml, and requests to model_maker BUILD

### Framework and core calculator improvements

- Loading resources through calculator and subgraph contexts and configuring through kResourcesService.
- Use std::make_unique
- Moves OnDiskCacheHelper class into a separate file / compilation target
- Pools: report buffer specs on failure, fix status propagation, fix includes
- Open-Source MediaPipe's WebGPU helpers.
- BatchMatul uses transpose parameter.
- Introduce Resource to represent a generic resource (file content, embedded/in-memory resource) for reading.
- Bump up the version number to 0.10.16
- Migrate from AdapterProperties to AdapterInfo
- Migrate from Resource::ReadContents to Resources::Get (using ForEachLine where required)
- Update Resources docs to mention ForEachLine (so devs don't fallback to ReadContents in such a case)
- Adjust WebGPU device registration
- Fix includes/copies/checks for BuildLabelMapFromFiles
- Migrate to BuildLabelMapFromFiles.
- Update Python version requirements in setup.py
- Introduce Resources with mapping, so graphs can use placeholders instead of actual resource paths.
- Remove Resources::ReadContents & add Resource::TryReleaseAsString.
- Fix ports for multi side outputs.
- Update solution android apps with explicit exported attribute.
- Ensure kResourcesService is set before CalculatorGraph is initialized (otherwise subgraphs/nodes may get the wrong default resources).
- Switch inference tests to ResourceProviderCalculator & update builder to refer MODEL_RESOURCE.
- Migrate modules to use ResourceProviderCalculator.
- Support single tensor input in TensorsToImageCalculator
- Migrate TfLiteModelLoader to use MP Resources.
- Remove deprecated TfLiteModelLoader::LoadFromPath.
- Fix for isIOS() platform util on worker and non-worker contexts
- Support single tensor input in TensorsToSegmentationCalculator
- Makes CalculatorContext::GetGraphServiceManager() private
- BatchMatMul can handle cases where ndims != 4 and quantization
- RmsNorm has an optional scale parameter.
- Allowed variable audio packet size by setting num_samples to null.
- Fix technically correct but confusing example in top level comments.
- Removing `ReturnType` helper, since it's part of the standard now.
- Update XNNPack to 9/24
- Enable LoRA conversion support for Gemma2-2B
- Improve warning when InferenceCalculator backends are not linked
- Bump MediaPipe version to 0.10.17.
- Update OpenCV to a version that compiles with C++ 17
- Force xnnpack when CPU inference is enforced
- Install PyBind before TensorFlow to get the MediaPipe version
- Change MP version to 0.10.18
- Add validation to LLM bundler, alternative takePicture method to support custom thread executor, CopySign op, const Spec() method to OutputStreamManager, support for converting SRGBA ImageFrame to YUVImage, model configuration parameters for Gemma2-2B, support for converting SRGBA ImageFrame to YUVImage, model configuration parameters for Gemma2-2B, menu for the default demo app and option to Close processor/graph and Exit gracefully, ngrammer, per layer embeddings and Relu1p5 fields to llm_params and update from Proto, a special InMemory Resources (current use case is in tests, but may be needed for some simple things as well), ResourceProviderCalculator (replacement for LocalFileContentsCalculator), Resource support into TfliteModelCalculator and a flag to set the default number of XNNPACK threads.

### MediaPipe Tasks update

This section should highlight the changes that are done specifically for any platform and don't propagate to
other platforms.

#### Javascript
- Add comments to generateResponses method.
- Migrate to ForEachLine to have a single source of truth for getting file contents lines.
- Workaround for multi-output web LLM issue where last response can get corrupted when numResponses is odd.
- Quick fix for wrong number of multi-outputs sometimes when streaming

### MediaPipe Dependencies

- Update WASM files


## v0.10.15

- Version: v0.10.15
- GitHub release published: 2024-08-30T19:09:23Z
- npm `@mediapipe/tasks-vision@0.10.15` published: 2024-08-29T18:05:17.105Z
- Source: https://github.com/google-ai-edge/mediapipe/releases/tag/v0.10.15
- Trimmed: removed platform-only sub-sections (Android: 1 items, iOS: 9 items, Python: 1 items, Model Maker changes: 8 items); everything else is verbatim.

### ​Build changes
- Fix unwanted dependency on GPU libraries.
- Adds TwoTapFirFilterCalculator.
- Add public visibility to `graph_service` headers.
- Disable ASAN, TSAN and MSAN tests which take more than 10 minutes.

### Framework and core calculator improvements
- Update `PointToForeign` with an optional cleanup object.
- Enable `BeginLoopCalculator` for move-only types (e.g. `Tensor`) without `Packet::Consume` usage and copyable types without copying unless it's a fundamental type.
- Ensure proper release of resources in case of multiple AHWB reads.
- Enables the configuration of GpuBufferPool options via GpuResources::Create();
- Bugfix to correctly handle landmark projection in the non-square case.
- add utility to wait for a sync (represented by FD)
- Change a RET_CHECK to RET_CHECK_EQ
- KinematicPathSolver: Avoid overshooting target
- Introduce GetDefaultGpuExecutor(GpuResources) to allow executing all calculators on MP GPU thread.
- No destruction for static ahwb_usage_track_.
- Unbind framebufffer in Affine Transformation Runner GL
- Move/isolate ahwb_usage_track_ into tensor_ahwb
- Guard ahwb_tensor_track_ with mutex.
- Add SidePacketConnectionTest
- Update C++ Graph Builder to support executors and support input/output stream handlers.
- Node::Input/OutputStreamHandler -> Node::SetInput/OutputStreamHandler
- Add `Packet::Share()` method in replacement of `SharedPtrWithPacket()` function.
- Default to high-performance power preference hint for WebGL contexts. For some computers with dual GPUs (like MBP2019), this will more frequently give us the higher performance GPU, which is generally preferable for most of our use cases (realtime rendering and ML), since speed is more critical than power consumption. If necessary, the user can override this setting by requesting their canvas' WebGL context manually before initializing the graph.
- Introduce input_scale parameter to SpectogramCalculator.
- Improve documentation of graph options
- Add an option to PackMediaSequenceCalculator to add empty clip labels instead of ignoring them. This is useful when we want to distinguish processing errors from no-detections.
- Updates language detection headers
- Fix dangling error reporter pointer in memory mapped models
- Fix for possible infinite stall using setOptions immediately before a loadLoraModel call.
- Add relu1p5 op, abs op, Log op, mdspan and Lhs Broadcast Sub with test
- Fix missing member move in Tensor class
- Add support for single Tensor output streams for ImageToTensorCalculator.
- Fix some compilation errors in WebGPU code. These changes are all minor.
- Add single tensor output support to tensor_converter_calculator.
- Replace QCHECK with ABSL_QCHECK and CHECK with ABSL_CHECK.
- Fix a bug in TensorAHWB that triggers a crash with multiple delayed AHWB readers followed by a CPU reader.
- Fixes an unnecessary allocation of GraphServiceManager in case it is adopted from the calculator context.
- Fix triggering of DFATAL message.
- Remove xnn_enable_avx512fp16=false from .bazelrc
- Replace uses of TfLiteOperatorCreate with TfLiteOperatorCreateWithData
- Compile with '--keep_going' in setup.py
- Update ndk version so that our open source users get the best possible performance out of mediapipe.
- Correct address of android ndk
- Replace absl::make_unique with std::make_unique in tensor.cc and tensor_ahwb.cc.
- LLM decode benchmarks fill the cache with a predefined number of tokens before starting decoding.
- Add logic to drop the offending non-monotonically increasing timestamp in the MicrophoneHelper.
- Make packet payload const.
- Pass flag to indicate that consuming op may support prepacked GEMM.
- Get timestamp from OpenCV VideoCapture after first frame is read.
- Update XNNPack and cpuinfo
- Update TensorFlow to 2024-07-18.
- Remove deprecated TfLiteOperatorCreateWithData function
- Add option to use shifted window in SpectrogramCalculator.
- Move AhwbUsage struct and helper methods into a separate library.
- Make fields in `PacketGetter.Pair` public.
- The GraphProfiler my be destoried before the task executed in the executor.
- Introduce flag in MicrophoneHelper to drop non-increasing timestamps.
- llm_test - add batch size of 8 for BM_Llm_QCINT8/512/128
- Add method to create MP Tensor from TfLite tensor specs
- Refactors AHardwareBufferView class to be instantiated with a TensorAhwbUsage pointer.
- Refactor LlmBuilder to have one graph
- Add `expected_seq_len` param to ComputeLogits()
- Fix mediapipe::file::Exists() for >2GB files on Windows.
- Bump XNNPACK and KleidiAI versions.
- Update MP demo app to acquire wake lock
- Replace mediapipe::StatusOr with absl::StatusOr
- Sync on ssbo_writte_ before mapping an AHWB to a CpuReadView.
### MediaPipe Tasks update
This section should highlight the changes that are done specifically for any platform and don't propagate to
other platforms.
#### Javascript
- Set quantization bits for LoRA weight conversion to match those specified
- Warn on adding packets to a closed input stream instead of silently dropping packets.
- Enable experimental support for Chromium WGSL subgroups in LLM API, when available.
- Support multi-response generation.
### Bug fixes
- class_weights flag cuases a crash for multiclass case
### MediaPipe Dependencies
- Update WASM files


## What this unlocks for the kaleidoscope

Plain summary: the 0.10.14 to 1.2.3 notes contain very little that speeds up or cools the web hand landmarker. Almost all of the 17 releases are Android, iOS, Python, LiteRT and LLM/GenAI work. The web-relevant hand-tracking items are small (a power-preference default, a worker/module loading fix, an IIFE bundle, a per-frame running-mode cache, a doc clarification about handedness). The API surface the app uses did not change in a way that opens new performance options: in the 1.2.3 `vision.d.ts`, `BaseOptions.delegate` is still only `"CPU" | "GPU"` (no WebGPU or LiteRT/NPU choice on the web), `ImageSource` is still `TexImageSource` (identical to 0.10.14), `VisionTaskOptions.canvas` is still `HTMLCanvasElement | OffscreenCanvas`, and `detectForVideo(videoFrame: ImageSource, timestamp, imageProcessingOptions?)` is unchanged. The WebGPU work in v0.10.32, v1.0.0 and v0.10.18 is internal plumbing for the web LLM/GenAI tasks and is not exposed for `HandLandmarker`.

Legend: "Supported by changelog" means a release note (and, where stated, the type definitions or bundle) backs the change. "Inference" means my reasoning about this app, which the changelog does not claim and which must be measured.

### Check before shipping (not an optimisation, but it is new)

**Telemetry code now ships in the web bundle.** Evidence: v1.0.0 "Add MediaPipe Tasks Privacy Notice" and "Add dummy logging framework", v1.1.0 "Add stats logging to Web Tasks", v1.2.0 "Support Web Worker platform detection and Flutter Web telemetry overrides", v1.2.3 "Forward BaseOptions app telemetry". In the 1.2.3 `vision_bundle.cjs` I found a batching logger that POSTs protobuf to `https://odml.pa.googleapis.com/v1/log` every 60 seconds with an `x-goog-api-key` header, and a task-name table that includes `HandLandmarker`; it is constructed inside the task factory on every `createFromOptions`. The 0.10.14 bundle contains no such URL. `vision.d.ts` 1.2.3 also adds an undocumented `TaskRunner.enableLogging(options)`. What I could not determine statically: whether it actually transmits in a default run (the API key comes from the wasm side) or whether any option turns it off. Action: open the worker's Network panel (or a proxy) with Fingertips on for two minutes and look for POSTs to that host. If they appear and that is not acceptable for a webcam app, either pin back to 0.10.14 (or 0.10.35, which has none of the new logging notes) or block the host via CSP `connect-src` (the logger records an error and stops on a network failure, per the bundle code, so blocking should be harmless, but test it). This is an inference from code reading, not a confirmed behaviour.

**Download size went up.** Checked on jsDelivr: `wasm/vision_wasm_internal.wasm` is 14,665,716 bytes in 1.2.3 versus 9,423,986 in 0.10.14 (+55%), and `vision_bundle.cjs` is 156 KB versus 137 KB. v1.2.2 "Remove non-SIMD WebAssembly builds" removed `vision_wasm_nosimd_internal.*` (the app never used it, since SIMD-capable browsers pick the SIMD build), and `FilesetResolver.isSimdSupported()` no longer exists in the types. The package layout is not quite unchanged: 1.2.3 adds `vision_bundle.js` (an IIFE defining a global `Vision`) and `wasm/vision_wasm_module_internal.{js,wasm}` (the ES-module variant, selected by the new `useModule` argument of `forVisionTasks`). Cold start on a poor connection is therefore slower; steady-state per-frame cost is unaffected as far as the notes say. Mitigation (inference): host the wasm and model with long-lived caching, or cache them with the Cache API, and show the existing "Hands ready" toast only after load.

### Recommendations, ranked

1. **A/B the CPU delegate against GPU, and confirm which backend you actually get.** (Backend logging: supported by changelog, v0.10.20 "Improve logging to allow users to understand which InferenceCalculator backend is used ... and when a model is loaded (including its size)". The benefit: inference.)
   - Change: in the worker, construct the landmarker with `delegate: 'CPU'` behind a debug flag and compare against `'GPU'` while the kaleidoscope runs at its normal frame rate; read the worker console to confirm that `'GPU'` is not silently falling back. 1.2.2 removed the non-SIMD build, so the CPU path is now always the SIMD build.
   - Expected benefit: the hand model is small, the worker already isolates it from the main thread, and the GPU delegate competes with a 120 fps WebGL2 renderer on the same GPU, plus texture upload from an ImageBitmap each frame. CPU inference could reduce GPU contention and frame-time jitter; whether it lowers total heat is unknown.
   - Risk: CPU inference latency in a single wasm thread (the notes mention no multithreaded web build) may raise tracking lag or, in a worker, burn CPU cores instead. Measure frame time of the renderer, tracking latency and package power (e.g. `powermetrics`) for both. Keep the existing GPU-then-CPU fallback.

2. **Send a smaller, slower stream to the model.** (Inference; independent of the version bump.)
   - Change: the worker's input is a 480 px wide bitmap sent up to ~30/s. Try 256 to 320 px wide and a ~20 to 24 Hz ceiling while a hand is present (and keep the ~7/s idle rate), or adapt the rate to the renderer's measured frame time. Not tied to any changelog item.
   - Expected benefit: less `createImageBitmap` resize work on the main thread, less upload and less inference work per second, which is the most direct lever on heat.
   - Risk: fingertip jitter and missed fast gestures; palm detection of small or distant hands degrades at low resolution. Test with fast strokes and two hands.

3. **Control the WebGL power preference of MediaPipe's own context.** (Supported by changelog: v0.10.15 "Default to high-performance power preference hint for WebGL contexts ... For some computers with dual GPUs (like MBP2019) ... If necessary, the user can override this setting by requesting their canvas' WebGL context manually before initializing the graph.")
   - Change: if the GPU delegate stays, create `const c = new OffscreenCanvas(1, 1); c.getContext('webgl2', { powerPreference: 'low-power' });` in the worker and pass it as `canvas: c` in `HandLandmarker.createFromOptions`. The 1.2.3 types say the task throws only if a different type of context was already created on that canvas, so a `webgl2` context created first should be accepted; I did not run this.
   - Expected benefit: this was a default change you inherited by upgrading from 0.10.14. On a dual-GPU Intel Mac it can pull the discrete GPU into the loop and raise heat; keeping the tracker on the integrated GPU while the renderer uses whatever it uses avoids that. On Apple Silicon (a single GPU) it has no effect. I could not verify from the minified code whether 0.10.14's default differed, I am relying on the release note.
   - Risk: low-power may be slower for inference; verify on the target machine.

4. **Load the bundle with `importScripts` instead of `fetch` + `new Function`.** (Supported by changelog: v1.0.0 "Add IIFE bundles for web task modules"; v0.10.32 "Web Solutions: patch for importScripts error with modules in workers"; v0.10.35 "Allow MP Task files to be use in Vite's workers". Verified present: `vision_bundle.js` in 1.2.3 starts with `var Vision=function(t){...`, and the bundle's loader tries `importScripts` then falls back to `self.import`/`import()`.)
   - Change: in the Blob worker, `importScripts('<cdn>/vision_bundle.js')` and use `Vision.FilesetResolver` / `Vision.HandLandmarker`. Alternatively a module worker with `vision_bundle.mjs` and `forVisionTasks(path, true)` (the `useModule` argument is in the 1.2.3 types). I have not confirmed that the IIFE's global exposes `HandLandmarker` and `FilesetResolver` at runtime; check `Object.keys(Vision)`.
   - Expected benefit: no eval, no second fetch of the script text, standard browser caching, works under a CSP without `unsafe-eval`. No expected runtime speed-up.
   - Risk: the Blob worker's base URL is `blob:`; `importScripts` needs an absolute URL (you already use an absolute CDN URL). Re-test the GPU to CPU fallback path.

5. **Keep input as ImageBitmap, but consider handing the worker a `VideoFrame` stream.** (Inference. `ImageSource = TexImageSource` in both versions, so VideoFrame, ImageBitmap, OffscreenCanvas and video elements were already accepted; nothing new here.)
   - Change: in Chromium, transfer the camera `MediaStreamTrack` to the worker and read frames with `MediaStreamTrackProcessor`, passing each `VideoFrame` straight to `detectForVideo`, then `frame.close()`. This removes the main-thread `createImageBitmap` and the per-frame `postMessage`.
   - Expected benefit: less main-thread work contending with the render loop. Small.
   - Risk: Chromium-only (Safari and Firefox lack or differ on this), the full-resolution frame feeds the model unless you downscale, and it is a larger rewrite than items 1 to 3. Probably not worth it unless profiling shows the main-thread bitmap step is significant.

6. **Free: running mode is cached per frame.** (Supported by changelog: v1.0.0 "Cache running mode in VisionTaskRunner".) You already create the landmarker in `'VIDEO'` mode and never switch; there is nothing to change and the gain is tiny. Listed so nobody looks for it.

### Compatibility notes found while reading (not optimisations)

- `handednesses` is deprecated in favour of `handedness` (v1.2.3, "Unify MediaPipe Tasks option, method, and result names ... preserving deprecated compatibility aliases"; confirmed in the 1.2.3 types for `HandLandmarkerResult` and `GestureRecognizerResult`). The worker already reads `r.handedness || r.handednesses`.
- v1.2.2 "Clarify in the `HandLandmarkerResult` ... documentation ... that `Left` and `Right` handedness correspond to the subject's hands rather than the viewer's, and that passing a horizontally mirrored image inverts these outputs." This is a documentation change as written, not a stated behaviour change. The app sends an unmirrored camera frame; if the painter/grip logic assumes a mirrored preview, verify the labels empirically once rather than trusting old assumptions.
- Removed from the web types: `FaceStylizer`, `FilesetResolver.isSimdSupported` (neither is used by the app). `InteractiveSegmenter` was split into a new class plus `InteractiveSegmenterLegacy`. `NormalizedLandmark` gained an optional `presence` (v1.1.0 "Expose per-landmark presence scores on the Web Tasks API"); its doc says it is unset when the model does not output one, so do not expect it for the unchanged hand model.

### Things that look relevant but are not

- WebGPU: no web delegate option for HandLandmarker in 1.2.3 (only `"CPU" | "GPU"`), so nothing to switch on.
- LiteRT / NPU / `use_litert` options (v1.1.0, v1.1.1, v1.2.x): native Android/iOS/C API only; absent from the web `BaseOptions`.
- GPU memory-leak fix, "Fix GPU memory leaks when releasing AHWB-backed SSBOs and pooled textures" (v1.2.0) and AHardwareBuffer work: AHWB is Android; not applicable to the browser.
- "Fix landmark drift in LandmarksSmoothingCalculator when stream resolution dynamically changes" (v1.1.0) and "configurable policy for handling empty landmarks in smoothing calculators" (v0.10.35): native graph changes, not exposed as web options; the app does its own smoothing.
- Worker fixes: "Fix DrawingUtils constructor failing in Web Workers" (v0.10.22) matters only if the worker used `DrawingUtils`, which it does not.
- Model: the hand model file is unchanged since April 2023 (given), and no release note announces a new hand model or a faster hand pipeline for the web.
