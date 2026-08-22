#import "MSFGalleryViewController.h"
#import "MSFExampleCatalogue.h"
#import "MSFExampleViewController.h"
#import "DemoViewController.h"

@interface MSFGalleryCell : UICollectionViewCell
@property (nonatomic, strong) UIImageView *shot;
@property (nonatomic, strong) UILabel *titleLabel;
@property (nonatomic, strong) UILabel *summaryLabel;
@end

@implementation MSFGalleryCell

- (instancetype)initWithFrame:(CGRect)frame {
    if ((self = [super initWithFrame:frame])) {
        self.contentView.layer.cornerRadius = 16;
        self.contentView.layer.borderWidth = 1;
        self.contentView.layer.borderColor = UIColor.separatorColor.CGColor;
        self.contentView.clipsToBounds = YES;
        self.contentView.backgroundColor = UIColor.secondarySystemGroupedBackgroundColor;

        _shot = [[UIImageView alloc] init];
        _shot.contentMode = UIViewContentModeScaleAspectFill;
        _shot.clipsToBounds = YES;
        _shot.backgroundColor = UIColor.tertiarySystemFillColor;

        _titleLabel = [[UILabel alloc] init];
        _titleLabel.font = [UIFont preferredFontForTextStyle:UIFontTextStyleHeadline];

        _summaryLabel = [[UILabel alloc] init];
        _summaryLabel.font = [UIFont preferredFontForTextStyle:UIFontTextStyleFootnote];
        _summaryLabel.textColor = UIColor.secondaryLabelColor;
        _summaryLabel.numberOfLines = 0;

        UIStackView *text = [[UIStackView alloc] initWithArrangedSubviews:@[ _titleLabel, _summaryLabel ]];
        text.axis = UILayoutConstraintAxisVertical;
        text.spacing = 2;
        text.layoutMarginsRelativeArrangement = YES;
        text.directionalLayoutMargins = NSDirectionalEdgeInsetsMake(10, 14, 14, 14);

        UIStackView *stack = [[UIStackView alloc] initWithArrangedSubviews:@[ _shot, text ]];
        stack.axis = UILayoutConstraintAxisVertical;
        stack.translatesAutoresizingMaskIntoConstraints = NO;
        [self.contentView addSubview:stack];
        [NSLayoutConstraint activateConstraints:@[
            [stack.topAnchor constraintEqualToAnchor:self.contentView.topAnchor],
            [stack.leadingAnchor constraintEqualToAnchor:self.contentView.leadingAnchor],
            [stack.trailingAnchor constraintEqualToAnchor:self.contentView.trailingAnchor],
            [stack.bottomAnchor constraintLessThanOrEqualToAnchor:self.contentView.bottomAnchor],
            // The stored screenshot IS a wide vignette, so the band is fixed rather than the
            // image's own aspect.
            [_shot.heightAnchor constraintEqualToAnchor:_shot.widthAnchor multiplier:0.55],
        ]];
    }
    return self;
}

@end

@interface MSFGallerySectionHeader : UICollectionReusableView
@property (nonatomic, strong) UILabel *titleLabel;
@property (nonatomic, strong) UILabel *summaryLabel;
@end

@implementation MSFGallerySectionHeader

- (instancetype)initWithFrame:(CGRect)frame {
    if ((self = [super initWithFrame:frame])) {
        _titleLabel = [[UILabel alloc] init];
        _titleLabel.font = [UIFont preferredFontForTextStyle:UIFontTextStyleTitle2];
        _titleLabel.textColor = UIColor.systemGreenColor;
        _summaryLabel = [[UILabel alloc] init];
        _summaryLabel.font = [UIFont preferredFontForTextStyle:UIFontTextStyleSubheadline];
        _summaryLabel.textColor = UIColor.secondaryLabelColor;
        _summaryLabel.numberOfLines = 0;
        UIStackView *stack = [[UIStackView alloc] initWithArrangedSubviews:@[ _titleLabel, _summaryLabel ]];
        stack.axis = UILayoutConstraintAxisVertical;
        stack.spacing = 2;
        stack.translatesAutoresizingMaskIntoConstraints = NO;
        [self addSubview:stack];
        [NSLayoutConstraint activateConstraints:@[
            [stack.topAnchor constraintEqualToAnchor:self.topAnchor constant:14],
            [stack.leadingAnchor constraintEqualToAnchor:self.leadingAnchor constant:4],
            [stack.trailingAnchor constraintEqualToAnchor:self.trailingAnchor constant:-4],
            [stack.bottomAnchor constraintEqualToAnchor:self.bottomAnchor constant:-8],
        ]];
    }
    return self;
}

@end

@interface MSFGalleryViewController () <UICollectionViewDataSource, UICollectionViewDelegate, UICollectionViewDelegateFlowLayout>
@property (nonatomic, strong) UICollectionView *collectionView;
@property (nonatomic, copy) NSArray<MSFExampleSection *> *sections;
@end

@implementation MSFGalleryViewController

- (void)viewDidLoad {
    [super viewDidLoad];
    self.title = @"Massif Maps examples";
    self.view.backgroundColor = UIColor.systemGroupedBackgroundColor;
    _sections = [MSFExampleCatalogue sections];

    self.navigationItem.rightBarButtonItem =
        [[UIBarButtonItem alloc] initWithTitle:@"Bench"
                                         style:UIBarButtonItemStylePlain
                                        target:self
                                        action:@selector(openBench)];

    UICollectionViewFlowLayout *layout = [[UICollectionViewFlowLayout alloc] init];
    layout.sectionInset = UIEdgeInsetsMake(0, 16, 16, 16);
    layout.minimumInteritemSpacing = 12;
    layout.minimumLineSpacing = 12;
    layout.headerReferenceSize = CGSizeMake(0, 70);

    _collectionView = [[UICollectionView alloc] initWithFrame:self.view.bounds
                                        collectionViewLayout:layout];
    _collectionView.autoresizingMask = UIViewAutoresizingFlexibleWidth | UIViewAutoresizingFlexibleHeight;
    _collectionView.backgroundColor = UIColor.clearColor;
    _collectionView.dataSource = self;
    _collectionView.delegate = self;
    [_collectionView registerClass:[MSFGalleryCell class] forCellWithReuseIdentifier:@"cell"];
    [_collectionView registerClass:[MSFGallerySectionHeader class]
        forSupplementaryViewOfKind:UICollectionElementKindSectionHeader
               withReuseIdentifier:@"header"];
    [self.view addSubview:_collectionView];
}

- (void)openBench {
    [self.navigationController pushViewController:[[DemoViewController alloc] init] animated:YES];
}

- (NSInteger)numberOfSectionsInCollectionView:(UICollectionView *)collectionView {
    return _sections.count;
}

- (NSInteger)collectionView:(UICollectionView *)collectionView
     numberOfItemsInSection:(NSInteger)section {
    return _sections[section].examples.count;
}

- (UICollectionViewCell *)collectionView:(UICollectionView *)collectionView
                  cellForItemAtIndexPath:(NSIndexPath *)indexPath {
    MSFGalleryCell *cell = [collectionView dequeueReusableCellWithReuseIdentifier:@"cell"
                                                                     forIndexPath:indexPath];
    MSFExampleEntry *entry = _sections[indexPath.section].examples[indexPath.item];
    cell.titleLabel.text = entry.title;
    cell.summaryLabel.text = entry.summary;
    cell.shot.image = entry.screenshot;
    // An example the manifest lists and iOS has not ported reads as unavailable rather than
    // vanishing - the gap is the useful information.
    cell.contentView.alpha = entry.exampleClass ? 1.0 : 0.45;
    return cell;
}

- (UICollectionReusableView *)collectionView:(UICollectionView *)collectionView
           viewForSupplementaryElementOfKind:(NSString *)kind
                                 atIndexPath:(NSIndexPath *)indexPath {
    MSFGallerySectionHeader *header =
        [collectionView dequeueReusableSupplementaryViewOfKind:kind
                                          withReuseIdentifier:@"header"
                                                 forIndexPath:indexPath];
    header.titleLabel.text = _sections[indexPath.section].title;
    header.summaryLabel.text = _sections[indexPath.section].summary;
    return header;
}

- (CGSize)collectionView:(UICollectionView *)collectionView
                  layout:(UICollectionViewLayout *)layout
  sizeForItemAtIndexPath:(NSIndexPath *)indexPath {
    CGFloat available = collectionView.bounds.size.width - 32;
    // A card wants about 190pt to keep its screenshot readable.
    NSInteger columns = MAX(1, (NSInteger)(available / 190));
    CGFloat width = (available - 12 * (columns - 1)) / columns;
    return CGSizeMake(floor(width), floor(width * 0.55) + 96);
}

- (void)collectionView:(UICollectionView *)collectionView
didSelectItemAtIndexPath:(NSIndexPath *)indexPath {
    [collectionView deselectItemAtIndexPath:indexPath animated:YES];
    MSFExampleEntry *entry = _sections[indexPath.section].examples[indexPath.item];
    if (!entry.exampleClass) {
        return;
    }
    [self.navigationController pushViewController:
        [[MSFExampleViewController alloc] initWithEntry:entry] animated:YES];
}

@end
