#import <UIKit/UIKit.h>
#import "MSFExampleCatalogue.h"

NS_ASSUME_NONNULL_BEGIN

/**
 * An example's own source, read from the app bundle.
 *
 * The build copies the Examples folder in as a resource, so what is shown here is the file that
 * ran - not a transcription that can drift from it. The website shows the same file, read from
 * the repo.
 */
@interface MSFExampleCodeViewController : UIViewController
- (instancetype)initWithEntry:(MSFExampleEntry *)entry;
@end

NS_ASSUME_NONNULL_END
