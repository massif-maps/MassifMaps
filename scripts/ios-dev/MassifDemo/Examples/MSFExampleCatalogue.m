#import "MSFExampleCatalogue.h"
#import <objc/runtime.h>

@interface MSFExampleEntry ()
@property (nonatomic, copy) NSString *identifier;
@property (nonatomic, copy) NSString *title;
@property (nonatomic, copy) NSString *summary;
@property (nonatomic, copy) NSString *section;
@property (nonatomic, nullable) Class exampleClass;
@property (nonatomic, copy, nullable) NSString *sourceName;
@end

@implementation MSFExampleEntry

/** Read on demand: a gallery of thumbnails should not decode every full screenshot up front. */
- (UIImage *)screenshot {
    NSString *path = [[NSBundle mainBundle] pathForResource:self.identifier
                                                     ofType:@"png"
                                                inDirectory:@"screenshots"];
    return path ? [UIImage imageWithContentsOfFile:path] : nil;
}

- (NSString *)sourceCode {
    if (!self.sourceName) {
        return nil;
    }
    NSString *path = [[NSBundle mainBundle] pathForResource:self.sourceName
                                                     ofType:@"m"
                                                inDirectory:@"Examples"];
    return path ? [NSString stringWithContentsOfFile:path encoding:NSUTF8StringEncoding error:nil]
                : nil;
}

@end

@interface MSFExampleSection ()
@property (nonatomic, copy) NSString *identifier;
@property (nonatomic, copy) NSString *title;
@property (nonatomic, copy) NSString *summary;
@property (nonatomic, copy) NSArray<MSFExampleEntry *> *examples;
@end

@implementation MSFExampleSection
@end

@implementation MSFExampleCatalogue

/**
 * Every class in the app that conforms to MSFExample, keyed by its id.
 *
 * The Objective-C runtime can enumerate classes, so unlike Android this needs no generated
 * registry - the equivalent of ExampleRegistry.java is this loop.
 */
+ (NSDictionary<NSString *, Class> *)implementations {
    static NSDictionary *implementations;
    static dispatch_once_t once;
    dispatch_once(&once, ^{
        NSMutableDictionary *found = [NSMutableDictionary dictionary];
        unsigned int count = 0;
        Class *classes = objc_copyClassList(&count);
        for (unsigned int i = 0; i < count; i++) {
            Class candidate = classes[i];
            if (!class_conformsToProtocol(candidate, @protocol(MSFExample))) {
                continue;
            }
            if (![candidate respondsToSelector:@selector(exampleId)]) {
                continue;
            }
            NSString *identifier = [candidate exampleId];
            if (identifier.length) {
                found[identifier] = candidate;
            }
        }
        free(classes);
        implementations = found;
    });
    return implementations;
}

+ (NSArray<MSFExampleSection *> *)sections {
    static NSArray *sections;
    static dispatch_once_t once;
    dispatch_once(&once, ^{
        NSString *path = [[NSBundle mainBundle] pathForResource:@"examples" ofType:@"json"];
        NSData *data = path ? [NSData dataWithContentsOfFile:path] : nil;
        NSDictionary *manifest = data
            ? [NSJSONSerialization JSONObjectWithData:data options:0 error:nil] : nil;
        NSDictionary<NSString *, Class> *implementations = [self implementations];

        NSMutableArray *result = [NSMutableArray array];
        for (NSDictionary *rawSection in manifest[@"sections"]) {
            NSMutableArray *entries = [NSMutableArray array];
            for (NSDictionary *raw in rawSection[@"examples"]) {
                MSFExampleEntry *entry = [[MSFExampleEntry alloc] init];
                entry.identifier = raw[@"id"];
                entry.title = raw[@"title"];
                entry.summary = raw[@"description"];
                entry.section = rawSection[@"id"];
                entry.exampleClass = implementations[entry.identifier];
                entry.sourceName = entry.exampleClass ? NSStringFromClass(entry.exampleClass) : nil;
                [entries addObject:entry];
            }
            if (entries.count == 0) {
                continue;
            }
            MSFExampleSection *section = [[MSFExampleSection alloc] init];
            section.identifier = rawSection[@"id"];
            section.title = rawSection[@"title"];
            section.summary = rawSection[@"description"];
            section.examples = entries;
            [result addObject:section];
        }
        sections = result;
    });
    return sections;
}

+ (NSArray<MSFExampleEntry *> *)all {
    NSMutableArray *all = [NSMutableArray array];
    for (MSFExampleSection *section in [self sections]) {
        [all addObjectsFromArray:section.examples];
    }
    return all;
}

+ (MSFExampleEntry *)entryWithId:(NSString *)identifier {
    for (MSFExampleEntry *entry in [self all]) {
        if ([entry.identifier isEqualToString:identifier]) {
            return entry;
        }
    }
    return nil;
}

@end
