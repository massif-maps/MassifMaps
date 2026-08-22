#import "MSFExampleCodeViewController.h"

@implementation MSFExampleCodeViewController {
    MSFExampleEntry *_entry;
}

- (instancetype)initWithEntry:(MSFExampleEntry *)entry {
    if ((self = [super initWithNibName:nil bundle:nil])) {
        _entry = entry;
    }
    return self;
}

- (void)viewDidLoad {
    [super viewDidLoad];
    self.title = _entry.title;
    self.view.backgroundColor = UIColor.systemBackgroundColor;

    UITextView *text = [[UITextView alloc] initWithFrame:self.view.bounds];
    text.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
    text.editable = NO;
    text.font = [UIFont monospacedSystemFontOfSize:11 weight:UIFontWeightRegular];
    text.text = _entry.sourceCode ?: @"Source not bundled - rebuild the app.";
    // Source lines are long and this is read, not edited.
    text.textContainer.lineBreakMode = NSLineBreakByClipping;
    [self.view addSubview:text];
}

@end
