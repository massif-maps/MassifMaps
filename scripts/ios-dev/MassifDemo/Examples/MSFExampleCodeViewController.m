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

    UITextView *text = [[UITextView alloc] init];
    text.translatesAutoresizingMaskIntoConstraints = NO;
    text.editable = NO;
    text.font = [UIFont monospacedSystemFontOfSize:11 weight:UIFontWeightRegular];
    text.text = _entry.sourceCode ?: @"Source not bundled - rebuild the app.";
    // Source lines are long and this is read, not edited.
    text.textContainer.lineBreakMode = NSLineBreakByClipping;
    [self.view addSubview:text];

    // The view scrolls under the bars and the home indicator, so it is pinned to the view and the
    // TEXT is inset instead - constraining the view itself to the safe area would leave a blank
    // band above the indicator with the code cut off at it.
    UILayoutGuide *safe = self.view.safeAreaLayoutGuide;
    [NSLayoutConstraint activateConstraints:@[
        [text.topAnchor constraintEqualToAnchor:self.view.topAnchor],
        [text.bottomAnchor constraintEqualToAnchor:self.view.bottomAnchor],
        [text.leadingAnchor constraintEqualToAnchor:safe.leadingAnchor],
        [text.trailingAnchor constraintEqualToAnchor:safe.trailingAnchor],
    ]];
}

@end
