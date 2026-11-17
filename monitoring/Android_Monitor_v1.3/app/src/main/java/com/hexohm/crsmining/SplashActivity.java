package com.hexohm.crsmining;

import android.app.Activity;
import android.content.Intent;
import android.graphics.Color;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.view.Gravity;
import android.view.View;
import android.widget.FrameLayout;
import android.widget.ImageView;
import android.widget.LinearLayout;
import android.widget.TextView;
import android.graphics.Typeface;

/** Four-second introduction using the approved separate Ω circuit icon and HexΩhm wordmark. */
public final class SplashActivity extends Activity {
    private final Handler handler = new Handler(Looper.getMainLooper());
    private final Runnable finishSplash = () -> {
        if (!isFinishing()) {
            startActivity(new Intent(this, MainActivity.class));
            finish();
            overridePendingTransition(android.R.anim.fade_in, android.R.anim.fade_out);
        }
    };

    @Override public void onCreate(Bundle state) {
        super.onCreate(state);
        getWindow().setStatusBarColor(Color.rgb(8, 13, 24));
        getWindow().setNavigationBarColor(Color.rgb(8, 13, 24));
        getWindow().getDecorView().setSystemUiVisibility(0);

        FrameLayout root = new FrameLayout(this);
        root.setBackgroundColor(Color.rgb(8, 13, 24));
        LinearLayout stack = new LinearLayout(this);
        stack.setGravity(Gravity.CENTER);
        stack.setOrientation(LinearLayout.VERTICAL);
        int padding = dp(26);
        stack.setPadding(padding, dp(12), padding, dp(12));

        ImageView omega = new ImageView(this);
        omega.setImageResource(R.drawable.hexohm_omega);
        omega.setContentDescription("HexOhm Ω circuit emblem");
        omega.setAdjustViewBounds(true);
        omega.setScaleType(ImageView.ScaleType.FIT_CENTER);
        LinearLayout.LayoutParams olp = new LinearLayout.LayoutParams(dp(230), dp(230));
        stack.addView(omega, olp);

        ImageView word = new ImageView(this);
        word.setImageResource(R.drawable.hexohm_wordmark);
        word.setContentDescription("HexΩhm");
        word.setAdjustViewBounds(true);
        word.setScaleType(ImageView.ScaleType.FIT_CENTER);
        LinearLayout.LayoutParams wlp = new LinearLayout.LayoutParams(-1, dp(96));
        wlp.topMargin = dp(12);
        stack.addView(word, wlp);

        TextView sub = new TextView(this);
        sub.setText("CRS MINING");
        sub.setTextColor(Color.rgb(167, 183, 204));
        sub.setTextSize(15);
        sub.setTypeface(Typeface.DEFAULT, Typeface.BOLD);
        sub.setLetterSpacing(.22f);
        sub.setGravity(Gravity.CENTER);
        LinearLayout.LayoutParams slp = new LinearLayout.LayoutParams(-1, dp(38));
        slp.topMargin = dp(20);
        stack.addView(sub, slp);

        FrameLayout.LayoutParams flp = new FrameLayout.LayoutParams(-1, -2, Gravity.CENTER);
        root.addView(stack, flp);
        setContentView(root);
        handler.postDelayed(finishSplash, 4000);
    }

    private int dp(float n) { return Math.round(n * getResources().getDisplayMetrics().density); }
    @Override protected void onDestroy() {
        handler.removeCallbacks(finishSplash);
        super.onDestroy();
    }
}
