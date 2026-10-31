/*
 * See the LICENSE file at the top of the project tree for copyright and
 * license details.
 *
 * openi2pd: shared Material 3 view factory.
 *
 * The programmatic screens (torrents, address book and runtime admin)
 * build their rows in Java. Centralizing the touch-target and padding
 * contract here keeps them from drifting away from the XML screens,
 * which already use
 * @dimen/touch_target and logical (RTL-aware) padding.
 */

package org.openi2pd.router;

import android.content.Context;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.widget.FrameLayout;
import android.widget.LinearLayout;
import android.widget.ScrollView;

import androidx.core.view.ViewCompat;
import com.google.android.material.button.MaterialButton;

/**
 * Shared Material Views construction for programmatic screens. Callers
 * create and update these views on the Android main thread and retain
 * them only for the owning Fragment's view lifecycle.
 */
final class UiFactory {
    private UiFactory() {
    }

    /**
     * Scroll container plus its centred, width-capped column. The
     * programmatic screens previously duplicated this block with a
     * hard-coded 16dp inset and no width cap, so they stretched edge to
     * edge on a tablet while the XML screens did not.
     */
    record Column(ScrollView scroll, LinearLayout content) {}

    static Column scrollingColumn(Context context) {
        ScrollView scroll = new ScrollView(context);
        scroll.setFillViewport(true);
        LinearLayout content = new MaxWidthLinearLayout(context);
        content.setOrientation(LinearLayout.VERTICAL);
        int inset = context.getResources().getDimensionPixelSize(R.dimen.screen_padding);
        content.setPadding(inset, inset, inset,
                context.getResources().getDimensionPixelSize(R.dimen.screen_padding_bottom));
        scroll.addView(content,
                new FrameLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT,
                        ViewGroup.LayoutParams.WRAP_CONTENT, Gravity.CENTER_HORIZONTAL));
        return new Column(scroll, content);
    }

    /**
     * A Material 3 button with the documented minimum touch target and
     * RTL-aware row padding. Callers still set text/listeners/style.
     */
    static MaterialButton button(Context context) {
        MaterialButton button = new MaterialButton(context);
        int target = context.getResources().getDimensionPixelSize(R.dimen.touch_target);
        button.setMinHeight(target);
        int inset = context.getResources().getDimensionPixelSize(R.dimen.margin_medium);
        button.setPaddingRelative(inset, button.getPaddingTop(), inset, button.getPaddingBottom());
        return button;
    }

    /**
     * One-speech-utterance semantics for a programmatic row. A
     * multi-line button otherwise makes TalkBack read the label, the
     * separator and the badge as unrelated fragments. {@code
     * ViewCompat.setStateDescription} degrades gracefully below API 30
     * while the content description remains available on every
     * supported level.
     */
    static void applyRowSemantics(View view, CharSequence description) {
        view.setContentDescription(description);
        ViewCompat.setStateDescription(view, description);
    }
}
