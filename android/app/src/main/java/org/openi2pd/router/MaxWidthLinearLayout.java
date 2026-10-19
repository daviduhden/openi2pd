/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: MaxWidthLinearLayout.
 *
 * LinearLayout that caps the width MeasureSpec at
 * {@code content_max_width} on tablets and unfolded devices while leaving
 * phones at their natural width. It only rewrites the width spec and delegates
 * measurement to {@link LinearLayout}.
 *
 */

package org.openi2pd.router;

import android.content.Context;
import android.util.AttributeSet;
import android.widget.LinearLayout;

/**
 * Vertical content container capped to a comfortable reading width.
 *
 * <p>Phones measure at their natural width. On tablets and unfolded devices the
 * view is measured at most at {@code content_max_width} and the surrounding
 * scroll container centres it, so cards and charts do not stretch across the
 * whole panel.</p>
 */
public final class MaxWidthLinearLayout extends LinearLayout {
    private final int maxWidth;

    public MaxWidthLinearLayout(Context context) {
        this(context, null);
    }

    public MaxWidthLinearLayout(Context context, AttributeSet attrs) {
        this(context, attrs, 0);
    }

    public MaxWidthLinearLayout(Context context, AttributeSet attrs, int defStyleAttr) {
        super(context, attrs, defStyleAttr);
        maxWidth = getResources().getDimensionPixelSize(R.dimen.content_max_width);
    }

    @Override
    protected void onMeasure(int widthMeasureSpec, int heightMeasureSpec) {
        int mode = MeasureSpec.getMode(widthMeasureSpec);
        int size = MeasureSpec.getSize(widthMeasureSpec);
        if (mode != MeasureSpec.UNSPECIFIED && maxWidth > 0 && size > maxWidth) {
            int cappedMode =
                    mode == MeasureSpec.AT_MOST ? MeasureSpec.AT_MOST : MeasureSpec.EXACTLY;
            widthMeasureSpec = MeasureSpec.makeMeasureSpec(maxWidth, cappedMode);
        }
        super.onMeasure(widthMeasureSpec, heightMeasureSpec);
    }
}
