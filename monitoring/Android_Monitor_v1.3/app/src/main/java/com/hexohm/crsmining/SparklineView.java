package com.hexohm.crsmining;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.view.View;

import java.util.ArrayList;
import java.util.Collection;
import java.util.List;

/** Compact, original line graph based only on locally collected poll results. */
final class SparklineView extends View {
    private final Paint p = new Paint(Paint.ANTI_ALIAS_FLAG);
    private List<Double> values = new ArrayList<>();
    private int lineColor;

    SparklineView(Context context, Collection<Double> points, int color) {
        super(context);
        values = new ArrayList<>(points);
        lineColor = color;
    }

    @Override protected void onDraw(Canvas canvas) {
        super.onDraw(canvas);
        float w = getWidth(), h = getHeight();
        if (w <= 0 || h <= 0) return;
        p.setColor(lineColor);
        p.setStrokeWidth(Math.max(2.2f, getResources().getDisplayMetrics().density * 2f));
        p.setStrokeCap(Paint.Cap.ROUND);
        p.setStrokeJoin(Paint.Join.ROUND);
        p.setStyle(Paint.Style.STROKE);
        p.setAlpha(70);
        canvas.drawLine(0, h - 5, w, h - 5, p);
        p.setAlpha(255);
        if (values.size() < 2) return;
        double max = 1;
        for (Double d : values) if (d != null && Double.isFinite(d)) max = Math.max(max, d);
        float prevX = 0, prevY = 0;
        for (int i = 0; i < values.size(); i++) {
            float x = (float) i / (values.size() - 1) * (w - 4) + 2;
            float y = h - 6 - (float) (Math.max(0, values.get(i)) / max) * (h - 14);
            if (i != 0) canvas.drawLine(prevX, prevY, x, y, p);
            prevX = x;
            prevY = y;
        }
    }
}
