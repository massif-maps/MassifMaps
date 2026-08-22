#import <Foundation/Foundation.h>
#import <UIKit/UIKit.h>

@class MSFMassifMap;

NS_ASSUME_NONNULL_BEGIN

/**
 * The screen an example runs on.
 *
 * Everything UIKit-shaped lives behind this so an example file reads as map code: a control is one
 * call, not a view plus a target plus a constraint. Mirrors Android's ExampleHost exactly, so the
 * two demos' examples can be read side by side.
 */
@protocol MSFExampleHost <NSObject>

/**
 * The map, already attached and ready. Registered under the example's own id, so two examples
 * cannot collide, and released - with every layer it built - when the screen goes away.
 */
@property (nonatomic, readonly) MSFMassifMap *map;

/** For anything that genuinely needs one: an asset, a scale, an alert. */
@property (nonatomic, readonly) UIViewController *viewController;

/** A line of text along the bottom telling the user what to do. nil or empty hides it. */
- (void)caption:(nullable NSString *)text;

/** A push button in the control row. */
- (void)button:(NSString *)label action:(void (^)(void))action;

/** An on/off button in the control row, starting in the given state. */
- (void)toggle:(NSString *)label on:(BOOL)on action:(void (^)(BOOL on))action;

/** A short message. Use sparingly - a caption is usually the better place. */
- (void)toast:(NSString *)text;

/** Runs something on the main queue after a delay, cancelled when the example stops. */
- (void)after:(NSTimeInterval)seconds run:(void (^)(void))action;

@end

/**
 * One example.
 *
 * The whole contract is -startWithHost:. Objective-C has no annotations, so the metadata is class
 * methods rather than Android's @ExampleInfo - and scripts/gen-examples.py matches an iOS example
 * to its Android twin by +exampleId, which is the thing both platforms and the website agree on.
 */
@protocol MSFExample <NSObject>

/** Kebab-case, and the SAME id as the Android example it mirrors. */
+ (NSString *)exampleId;

/**
 * Called once the map view has a size and the map is attached.
 *
 * On the MAIN queue, unlike Android: building a layer decodes its style, which is why Android
 * runs this off the UI thread - here the SDK's own decode already happens on its own queue.
 */
- (void)startWithHost:(id<MSFExampleHost>)host;

@optional
/** Only for things the host cannot release itself - a timer, a sensor. */
- (void)stop;

@end

NS_ASSUME_NONNULL_END
