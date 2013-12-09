#import <UIKit/UIKit.h>

@class EditorView;

@interface EditorPlateView : UIView

@property (nonatomic) NSString *editorName;
@property (nonatomic) UIImageView *arrowImageView;

+ (CGFloat) plateHeight;
- (id) initWithFrame : (CGRect) frame editorName : (NSString *) name topView : (EditorView *) tv;

@end
