#import <Foundation/Foundation.h>
#import "MSFExample.h"

NS_ASSUME_NONNULL_BEGIN

/** One example, resolved: its metadata from docs/examples/examples.json, its class from the app. */
@interface MSFExampleEntry : NSObject
@property (nonatomic, readonly) NSString *identifier;
@property (nonatomic, readonly) NSString *title;
@property (nonatomic, readonly) NSString *summary;
@property (nonatomic, readonly) NSString *section;
/** nil for an example the Android demo has but iOS has not ported yet. */
@property (nonatomic, readonly, nullable) Class exampleClass;
/** docs/examples/screenshots/<id>.png, bundled as a resource. */
@property (nonatomic, readonly, nullable) UIImage *screenshot;
/** The example's own .m, for the source screen. */
@property (nonatomic, readonly, nullable) NSString *sourceCode;
@end

@interface MSFExampleSection : NSObject
@property (nonatomic, readonly) NSString *identifier;
@property (nonatomic, readonly) NSString *title;
@property (nonatomic, readonly) NSString *summary;
@property (nonatomic, readonly) NSArray<MSFExampleEntry *> *examples;
@end

/**
 * The example list, read from the SAME docs/examples/examples.json the Android gallery and the
 * website use.
 *
 * The titles, descriptions, sections and screenshots therefore cannot drift between the three
 * surfaces - there is one file and it is generated from the examples themselves
 * (scripts/gen-examples.py). What this app supplies is the CLASS: an example is matched to its
 * entry by +exampleId, and one the manifest lists but iOS has not ported shows as unavailable
 * rather than being hidden.
 */
@interface MSFExampleCatalogue : NSObject

/** Every section that has at least one example, in the manifest's order. */
+ (NSArray<MSFExampleSection *> *)sections;

/** Every example, flattened. */
+ (NSArray<MSFExampleEntry *> *)all;

+ (nullable MSFExampleEntry *)entryWithId:(NSString *)identifier;

@end

NS_ASSUME_NONNULL_END
