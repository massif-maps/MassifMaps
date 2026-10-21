#import <Foundation/Foundation.h>

/**
 * In-process screenshots and a scripted camera, so a rendering bug can be reproduced from the
 * command line with no Screen Recording permission and no UI driving. Mirrors what the Android
 * BenchActivity does with intent extras; see docs/internals/catalyst-demo-loop.md.
 *
 *   -shot /tmp/a.png            one frame of the map's own render, then quit (-quitAfterShot false to stay)
 *   -shotSequence 60            a burst: <dir>/frame_0000.png ... + <dir>/frames.csv
 *   -shotIntervalMs 80          pause between two frames of the burst (default 80)
 *   -shotDir /tmp/dir           where the burst goes (default /tmp/massifdemo-shots/seq)
 *   -shotDelay 60               seconds after launch before the first frame (tile settle time)
 *   -rotateDegPerSec 10         bearing turns continuously, driven per display frame
 *   -rotateStepDeg 1            bearing +1 degree between two burst frames (deterministic)
 *   -zoomStep 0.01              zoom +0.01 between two burst frames (deterministic)
 *   -shotTilts 90,75,60,45,30,15  one still per tilt into <shotDir>/tilt_NN.png (-shotTiltWaitMs 6000 between)
 *   -duration 20                seconds the rotation runs; the app quits when it ends (unless a burst is running)
 */
@interface DemoCapture : NSObject

/** Reads the launch arguments; does nothing when none of the keys above is present. */
+ (void)startIfRequested;

@end
