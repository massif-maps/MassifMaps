#import "AppDelegate.h"
#import "DemoViewController.h"
#import "DemoLive.h"
#import "DemoCfg.h"
#import "Examples/MSFGalleryViewController.h"
#import "Examples/MSFExampleCatalogue.h"
#import "Examples/MSFExampleViewController.h"

@implementation AppDelegate

/**
 * The app opens on the example GALLERY, like Android's MainActivity. The composable
 * debugging/measurement map is the BENCH, reached from the toolbar or with `-bench true`.
 *
 *   xcrun simctl launch <device> com.massifmaps.MassifDemo -example terrain-3d -ui false
 *   xcrun simctl launch <device> com.massifmaps.MassifDemo -bench true -hillshade true
 */
- (BOOL)application:(UIApplication *)application didFinishLaunchingWithOptions:(NSDictionary *)options {
    self.window = [[UIWindow alloc] initWithFrame:[[UIScreen mainScreen] bounds]];

    MSFGalleryViewController *gallery = [[MSFGalleryViewController alloc] init];
    UINavigationController *navigation =
        [[UINavigationController alloc] initWithRootViewController:gallery];

    if ([DemoCfg boolFor:@"bench" defaultValue:NO]) {
        [navigation pushViewController:[[DemoViewController alloc] init] animated:NO];
    } else {
        NSString *identifier = [DemoCfg stringFor:@"example" defaultValue:nil];
        MSFExampleEntry *entry = identifier ? [MSFExampleCatalogue entryWithId:identifier] : nil;
        if (identifier && !entry) {
            NSLog(@"no example '%@'", identifier);
        }
        if (entry) {
            MSFExampleViewController *controller =
                [[MSFExampleViewController alloc] initWithEntry:entry];
            // '-ui false' strips the chrome, for a clean capture.
            controller.chromeHidden = ![DemoCfg boolFor:@"ui" defaultValue:YES];
            navigation.navigationBarHidden = controller.chromeHidden;
            [navigation pushViewController:controller animated:NO];
        }
    }

    self.window.rootViewController = navigation;
    [self.window makeKeyAndVisible];
    return YES;
}

/**
 * massifdemo://config?fog=false - the iOS answer to Android's CONFIG broadcast.
 *
 * A relaunch rebuilds every cache, which is exactly what hides a stale-redraw bug, so the bench
 * needs a way to change one knob on the RUNNING app. simctl can drive it:
 *
 *   xcrun simctl openurl <device> 'massifdemo://config?fog=false&zoom=14'
 */
- (BOOL)application:(UIApplication *)application
            openURL:(NSURL *)url
            options:(NSDictionary<UIApplicationOpenURLOptionsKey, id> *)options {
    if (![url.host isEqualToString:@"config"]) {
        return NO;
    }
    NSURLComponents *components = [NSURLComponents componentsWithURL:url resolvingAgainstBaseURL:NO];
    NSMutableDictionary<NSString *, NSString *> *values = [NSMutableDictionary dictionary];
    for (NSURLQueryItem *item in components.queryItems) {
        if (item.value) {
            values[item.name] = item.value;
        }
    }
    return [DemoLive apply:values];
}

@end
