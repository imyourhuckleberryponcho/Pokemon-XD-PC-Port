// xd_encode (macOS): the video encoder of the replay export with AVFoundation (H.264 video, AAC sound), the system's own: nothing shipped.
// UNTESTED: written without a Mac, never compiled by its author (the build does not include it unless cmake is given -DXD_BUILD_AVF_ENCODER=ON; the Mac
// workflow does that and runs a 30-frame smoke test with a one-second sound file). Same interface as xd_encode_mf.cpp:
//   xd_encode --size WxH --fps N --audio clip.wav --out video.mp4  < raw RGBA frames on stdin
// The WAV (PCM 16-bit, made by tools/showcase.py) only exists once the pictures have ended, so it cannot be written beside the pictures by one
// AVAssetWriter (the writer wants both tracks fed together). Three steps instead, each a plain system call and none of them re-encoding the picture:
//   1. the pictures -> <out>.video.mp4 (AVAssetWriter, H.264, no sound);
//   2. the WAV -> <out>.audio.m4a (AVAssetExportSession, preset AppleM4A = AAC; AVFoundation reads WAV files itself);
//   3. both tracks -> <out> (AVMutableComposition + AVAssetExportSession, preset Passthrough, MPEG-4).
// When step 2 or 3 fails the picture-only file is kept as <out> and a line says so (the same as the Windows encoder with an unreadable WAV).
#import <AVFoundation/AVFoundation.h>
#import <CoreVideo/CoreVideo.h>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

static const char* Why(NSError* e) { return e ? e.localizedDescription.UTF8String : "?"; }

// One export session run to its end. false (with a line on stderr) when it did not complete.
static bool RunExport(AVAsset* asset, NSString* preset, NSURL* to, AVFileType type) {
  AVAssetExportSession* ex = [[AVAssetExportSession alloc] initWithAsset:asset presetName:preset];
  if (!ex) {
    fprintf(stderr, "xd_encode: no export session for preset %s\n", preset.UTF8String);
    return false;
  }
  ex.outputURL = to;
  ex.outputFileType = type;
  dispatch_semaphore_t fin = dispatch_semaphore_create(0);
  [ex exportAsynchronouslyWithCompletionHandler:^{ dispatch_semaphore_signal(fin); }];
  dispatch_semaphore_wait(fin, DISPATCH_TIME_FOREVER);
  if (ex.status != AVAssetExportSessionStatusCompleted) {
    fprintf(stderr, "xd_encode: export (%s) failed: %s\n", preset.UTF8String, Why(ex.error));
    return false;
  }
  return true;
}

int main(int argc, char** argv) {
  @autoreleasepool {
    int w = 0, h = 0;
    double fps = 60000.0 / 1001.0;
    std::string out, audio;
    for (int i = 1; i + 1 < argc; i += 2) {
      std::string k = argv[i];
      if (k == "--size") std::sscanf(argv[i + 1], "%dx%d", &w, &h);
      else if (k == "--fps") fps = std::atof(argv[i + 1]);
      else if (k == "--audio") audio = argv[i + 1];
      else if (k == "--out") out = argv[i + 1];
    }
    if (w <= 0 || h <= 0 || out.empty()) {
      fprintf(stderr, "usage: xd_encode --size WxH [--fps N] [--audio clip.wav] --out video.mp4 < frames.rgba\n");
      return 1;
    }
    const bool wantAudio = !audio.empty();
    const std::string videoOut = wantAudio ? out + ".video.mp4" : out;
    NSURL* finalUrl = [NSURL fileURLWithPath:[NSString stringWithUTF8String:out.c_str()]];
    NSURL* url = [NSURL fileURLWithPath:[NSString stringWithUTF8String:videoOut.c_str()]];
    [[NSFileManager defaultManager] removeItemAtURL:url error:nil];
    NSError* err = nil;
    AVAssetWriter* writer = [AVAssetWriter assetWriterWithURL:url fileType:AVFileTypeMPEG4 error:&err];
    if (!writer) {
      fprintf(stderr, "xd_encode: cannot start the writer: %s\n", Why(err));
      return 2;
    }
    NSDictionary* vset = @{AVVideoCodecKey : AVVideoCodecTypeH264, AVVideoWidthKey : @(w), AVVideoHeightKey : @(h),
                           AVVideoCompressionPropertiesKey : @{AVVideoAverageBitRateKey : @((long long)w * h * 6)}};
    AVAssetWriterInput* vin = [AVAssetWriterInput assetWriterInputWithMediaType:AVMediaTypeVideo outputSettings:vset];
    vin.expectsMediaDataInRealTime = NO;
    NSDictionary* pset = @{(id)kCVPixelBufferPixelFormatTypeKey : @(kCVPixelFormatType_32BGRA), (id)kCVPixelBufferWidthKey : @(w), (id)kCVPixelBufferHeightKey : @(h)};
    AVAssetWriterInputPixelBufferAdaptor* ad = [AVAssetWriterInputPixelBufferAdaptor assetWriterInputPixelBufferAdaptorWithAssetWriterInput:vin sourcePixelBufferAttributes:pset];
    [writer addInput:vin];
    if (![writer startWriting]) {
      fprintf(stderr, "xd_encode: startWriting failed: %s\n", Why(writer.error));
      return 2;
    }
    [writer startSessionAtSourceTime:kCMTimeZero];
    std::vector<uint8_t> rgba((size_t)w * h * 4);
    long long n = 0;
    while (fread(rgba.data(), 1, rgba.size(), stdin) == rgba.size()) {
      while (!vin.readyForMoreMediaData) {
        if (writer.status == AVAssetWriterStatusFailed) {
          fprintf(stderr, "xd_encode: the writer failed: %s\n", Why(writer.error));
          return 2;
        }
        [NSThread sleepForTimeInterval:0.002];
      }
      CVPixelBufferRef pb = nullptr;
      if (CVPixelBufferCreate(nullptr, w, h, kCVPixelFormatType_32BGRA, nullptr, &pb) != kCVReturnSuccess || !pb) return 2;
      CVPixelBufferLockBaseAddress(pb, 0);
      uint8_t* base = (uint8_t*)CVPixelBufferGetBaseAddress(pb);
      size_t stride = CVPixelBufferGetBytesPerRow(pb);
      for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
          const uint8_t* s = &rgba[((size_t)y * w + x) * 4];
          uint8_t* d = base + y * stride + x * 4;
          d[0] = s[2]; d[1] = s[1]; d[2] = s[0]; d[3] = 255;
        }
      CVPixelBufferUnlockBaseAddress(pb, 0);
      const BOOL ok = [ad appendPixelBuffer:pb withPresentationTime:CMTimeMake(n * 1001, (int32_t)(fps * 1001.0 + 0.5))];
      CVPixelBufferRelease(pb);
      if (!ok) {
        fprintf(stderr, "xd_encode: a picture was refused: %s\n", Why(writer.error));
        return 2;
      }
      n++;
    }
    [vin markAsFinished];
    dispatch_semaphore_t done = dispatch_semaphore_create(0);
    [writer finishWritingWithCompletionHandler:^{ dispatch_semaphore_signal(done); }];
    dispatch_semaphore_wait(done, DISPATCH_TIME_FOREVER);
    if (writer.status != AVAssetWriterStatusCompleted) {
      fprintf(stderr, "xd_encode: the picture file was not finished: %s\n", Why(writer.error));
      return 2;
    }
    if (n == 0) return 3;
    if (!wantAudio) {
      fprintf(stderr, "xd_encode: %lld frames written to %s\n", n, out.c_str());
      return 0;
    }

    // Step 2: the sound (WAV -> AAC in an .m4a). A failure keeps the picture-only file under the final name.
    const std::string audioOut = out + ".audio.m4a";
    NSURL* aurl = [NSURL fileURLWithPath:[NSString stringWithUTF8String:audioOut.c_str()]];
    NSURL* wavUrl = [NSURL fileURLWithPath:[NSString stringWithUTF8String:audio.c_str()]];
    [[NSFileManager defaultManager] removeItemAtURL:aurl error:nil];
    bool muxed = false;
    if (![[NSFileManager defaultManager] fileExistsAtPath:wavUrl.path]) {
      fprintf(stderr, "xd_encode: the sound file %s is not there: no sound track\n", audio.c_str());
    } else if (RunExport([AVURLAsset URLAssetWithURL:wavUrl options:nil], AVAssetExportPresetAppleM4A, aurl, AVFileTypeAppleM4A)) {
      // Step 3: picture + sound into one MPEG-4 file, without touching either encoding.
      AVURLAsset* vAsset = [AVURLAsset URLAssetWithURL:url options:nil];
      AVURLAsset* aAsset = [AVURLAsset URLAssetWithURL:aurl options:nil];
      AVAssetTrack* vTrack = [[vAsset tracksWithMediaType:AVMediaTypeVideo] firstObject];
      AVAssetTrack* aTrack = [[aAsset tracksWithMediaType:AVMediaTypeAudio] firstObject];
      if (vTrack && aTrack) {
        AVMutableComposition* comp = [AVMutableComposition composition];
        AVMutableCompositionTrack* cv = [comp addMutableTrackWithMediaType:AVMediaTypeVideo preferredTrackID:kCMPersistentTrackID_Invalid];
        AVMutableCompositionTrack* ca = [comp addMutableTrackWithMediaType:AVMediaTypeAudio preferredTrackID:kCMPersistentTrackID_Invalid];
        NSError* e2 = nil;
        const CMTime vd = vAsset.duration;
        // (the sound is cut to the picture's length when it is longer; a shorter sound just ends early)
        const CMTime sd = CMTimeMinimum(aAsset.duration, vd);
        const BOOL ok1 = [cv insertTimeRange:CMTimeRangeMake(kCMTimeZero, vd) ofTrack:vTrack atTime:kCMTimeZero error:&e2];
        const BOOL ok2 = ok1 && [ca insertTimeRange:CMTimeRangeMake(kCMTimeZero, sd) ofTrack:aTrack atTime:kCMTimeZero error:&e2];
        if (ok1 && ok2) {
          [[NSFileManager defaultManager] removeItemAtURL:finalUrl error:nil];
          muxed = RunExport(comp, AVAssetExportPresetPassthrough, finalUrl, AVFileTypeMPEG4);
        } else {
          fprintf(stderr, "xd_encode: putting the tracks together failed: %s\n", Why(e2));
        }
      } else {
        fprintf(stderr, "xd_encode: a track is missing (picture %s, sound %s)\n", vTrack ? "yes" : "NO", aTrack ? "yes" : "NO");
      }
    }
    [[NSFileManager defaultManager] removeItemAtURL:aurl error:nil];
    if (muxed) {
      [[NSFileManager defaultManager] removeItemAtURL:url error:nil];
      fprintf(stderr, "xd_encode: %lld frames and the sound track written to %s\n", n, out.c_str());
    } else {
      // keep the picture-only file under the final name
      [[NSFileManager defaultManager] removeItemAtURL:finalUrl error:nil];
      NSError* e3 = nil;
      [[NSFileManager defaultManager] moveItemAtURL:url toURL:finalUrl error:&e3];
      fprintf(stderr, "xd_encode: %lld frames written to %s WITHOUT sound (see above)\n", n, out.c_str());
    }
    return 0;
  }
}
