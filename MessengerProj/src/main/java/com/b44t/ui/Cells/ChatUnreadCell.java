/*
 * This is the source code of Telegram for Android v. 3.x.x.
 * It is licensed under GNU GPL v. 2 or later.
 * You should have received a copy of the license in this archive (see LICENSE).
 *
 * Copyright Nikolai Kudashov, 2013-2016.
 */

package com.b44t.ui.Cells;

import android.content.Context;
import android.util.TypedValue;
import android.view.Gravity;
import android.widget.FrameLayout;
import android.widget.ImageView;
import android.widget.TextView;

import com.b44t.messenger.AndroidUtilities;
import com.b44t.messenger.R;
import com.b44t.ui.Components.LayoutHelper;
import com.b44t.ui.ActionBar.Theme;

public class ChatUnreadCell extends FrameLayout {

    private TextView textView;

    public ChatUnreadCell(Context context) {
        super(context);

        FrameLayout frameLayout = new FrameLayout(context);
        frameLayout.setBackgroundResource(R.drawable.newmsg_divider);
        addView(frameLayout, LayoutHelper.createFrame(LayoutHelper.MATCH_PARENT, 27, Gravity.LEFT | Gravity.TOP, 0, 7, 0, 0));

        textView = new TextView(context);
        textView.setPadding(0, 0, 0, AndroidUtilities.dp(1));
        textView.setTextSize(TypedValue.COMPLEX_UNIT_DIP, 14);
        textView.setTextColor(Theme.CHAT_UNREAD_TEXT_COLOR);
        textView.setTypeface(AndroidUtilities.getTypeface("fonts/rmedium.ttf"));
        addView(textView, LayoutHelper.createFrame(LayoutHelper.WRAP_CONTENT, LayoutHelper.WRAP_CONTENT, Gravity.CENTER));
    }

    public void setText(String text) {
        textView.setText(text);
    }

    @Override
    protected void onMeasure(int widthMeasureSpec, int heightMeasureSpec) {
        super.onMeasure(MeasureSpec.makeMeasureSpec(MeasureSpec.getSize(widthMeasureSpec), MeasureSpec.EXACTLY), MeasureSpec.makeMeasureSpec(AndroidUtilities.dp(40), MeasureSpec.EXACTLY));
    }
}
