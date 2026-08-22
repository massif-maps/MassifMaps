#import <UIKit/UIKit.h>
#import "MSFExample.h"
#import "MSFExampleCatalogue.h"

NS_ASSUME_NONNULL_BEGIN

/**
 * Runs one example.
 *
 * Owns every piece of UIKit an example would otherwise have to write: the map view, the back and
 * source buttons, the control row, the caption. The example itself only ever sees MSFExampleHost,
 * which is what keeps its source readable as documentation - the same split as Android's
 * ExampleActivity.
 */
@interface MSFExampleViewController : UIViewController <MSFExampleHost>

- (instancetype)initWithEntry:(MSFExampleEntry *)entry;

/** Hides the chrome, for a screenshot run. `-ui false` on the command line. */
@property (nonatomic) BOOL chromeHidden;

@end

NS_ASSUME_NONNULL_END
