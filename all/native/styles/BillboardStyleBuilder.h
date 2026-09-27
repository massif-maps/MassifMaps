/*
 * Copyright (c) 2016 CartoDB. All rights reserved.
 * Copying and using this code is allowed only according
 * to license terms, as given in https://cartodb.com/terms/
 */

#ifndef _MASSIF_BILLBOARDSTYLEBUILDER_H_
#define _MASSIF_BILLBOARDSTYLEBUILDER_H_

#include "styles/BillboardStyle.h"
#include "styles/StyleBuilder.h"

#include <memory>

namespace massif {
    class AnimationStyle;

    /**
     * A base class for BillboardStyleBuilder subclasses.
     */
    class BillboardStyleBuilder : public StyleBuilder {
    public:
        virtual ~BillboardStyleBuilder();
    
        /**
         * Returns the horizontal attaching anchor point of the billboard.
         * @return The horizontal attaching anchor point of the billboard.
         */
        float getAttachAnchorPointX() const;
        /**
         * Sets the horizontal attaching anchor point of the billboard.
         * @param attachAnchorPointX The new horizontal attaching anchor point for the billboard. -1 means the left side,
         * 0 the center and 1 the right side. The default is 0.
         */
        void setAttachAnchorPointX(float attachAnchorPointX);
        /**
         * Returns the vertical attaching anchor point of the billboard.
         * @return The vertical attaching anchor point of the billboard.
         */
        float getAttachAnchorPointY() const;
        /**
         * Sets the vertical attaching anchor point of the billboard.
         * @param attachAnchorPointY The new vertical attaching anchor point for the billboard. -1 means the bottom,
         * 0 the center and 1 the top. The default is 1.
         */
        void setAttachAnchorPointY(float attachAnchorPointY);
        /**
         * Sets the attaching anchor point for the billboard: the point on the base billboard where this
         * billboard is placed. Only used if the billboard is attached to another (base) billboard.
         * @param attachAnchorPointX The new horizontal attaching anchor point for the billboard. -1 means the left side,
         * 0 the center and 1 the right side. The default is 0.
         * @param attachAnchorPointY The new vertical attaching anchor point for the billboard. -1 means the bottom,
         * 0 the center and 1 the top. The default is 1.
         */
        void setAttachAnchorPoint(float attachAnchorPointX, float attachAnchorPointY);
        
        /**
         * Returns the state of the causes overlap flag.
         * @return True if this billboard causes overlapping with other billboards behind it.
         */
        bool isCausesOverlap() const;
        /**
         * Sets the state of the causes overlap flag. If true the billboard may hide overlapping billboards behind it
         * that have the hide if overlapped flag set; if false it never hides others. It may still be hidden by
         * billboards in front of it either way. The default depends on the subclass.
         * @param causesOverlap The new state of the allow overlap flag.
         */
        void setCausesOverlap(bool causesOverlap);
        /**
         * Returns the state of the allow overlap flag.
         * @return True if this billboard can be hidden by overlapping billboards in front of it.
         */
        bool isHideIfOverlapped() const;
        /**
         * Sets the state of the hide if overlapped flag. If true the billboard may be hidden by overlapping
         * billboards in front of it; if false it is never hidden by them. The default depends on the subclass.
         * @param hideIfOverlapped The new state of the hide if overlapped flag.
         */
        void setHideIfOverlapped(bool hideIfOverlapped);
    
        /**
         * Returns the horizontal offset of the billboard.
         * @return The horizontal offset of the billboard, units depend on the scaling mode.
         */
        float getHorizontalOffset() const;
        /**
         * Sets the horizontal offset of the bitmap, relative to the billboard rotation, in the units of setSize.
         * Negative values offset the bitmap to the left, positive to the right. The default is 0.
         * @param horizontalOffset The new horizontal offset for the billboard.
         */
        void setHorizontalOffset(float horizontalOffset);
        /**
         * Returns the vertical offset of the billboard.
         * @return The vertical offset of the billboard, units depend on the scaling mode.
         */
        float getVerticalOffset() const;
        /**
         * Sets the vertical offset of the bitmap, relative to the billboard rotation, in the units of setSize.
         * Negative values offset the bitmap to the bottom, positive to the top. The default is 0.
         * @param verticalOffset The new vertical offset for the billboard.
         */
        void setVerticalOffset(float verticalOffset);
    
        /**
         * Returns the placement priority of the billboard.
         * @return The placement priority of the billboard.
         */
        int getPlacementPriority() const;
        /**
         * Sets the placement priority for the billboard. Higher priority billboard get drawn in front of lower
         * priority billboards regardless of their distance to the camera. If billboards are not allowed to overlap then
         * higher priority billboards hide overlapping lower priority billboards. The default is 0.
         * @param placementPriority The new placement priority for the billboard.
         */
        void setPlacementPriority(int placementPriority);
        
        /**
         * Returns the state of the scale with DPI flag.
         * @return True if this billboard's size will be scaled using the screen dot's per inch.
         */
        bool isScaleWithDPI() const;
        /**
         * Sets the state of the scale with DPI flag. If true the billboard's size scales with screen DPI, so it looks the
         * same size on any density; if false it looks smaller on higher density screens (custom Label and Popup
         * implementations may compensate with higher resolution images). The default depends on the subclass.
         * @param scaleWithDPI The new state of the scale with DPI flag.
         */
        void setScaleWithDPI(bool scaleWithDPI);

        /**
         * Returns the animation style of the billboard.
         * @return The animation style of the billboard. Can be null if animations are not used.
         */
        std::shared_ptr<AnimationStyle> getAnimationStyle() const;
        /**
         * Sets the animation style of the billboard.
         * @param animStyle The new animation style of the billboard. Can be null if animations are not needed (the default).
         */
        void setAnimationStyle(const std::shared_ptr<AnimationStyle>& animStyle);
    
    protected:
        BillboardStyleBuilder();

        float _attachAnchorPointX;
        float _attachAnchorPointY;
        
        bool _causesOverlap;
        bool _hideIfOverlapped;
    
        float _horizontalOffset;
        float _verticalOffset;
    
        int _placementPriority;
        
        bool _scaleWithDPI;

        std::shared_ptr<AnimationStyle> _animationStyle;
    };
    
}

#endif
