/*
 * Copyright (c) 2013-2026, The PurpleI2P Project
 * Copyright (c) 2026, David Uhden Collado
 * SPDX-License-Identifier: BSD-3-Clause
 */
package org.openi2pd.router;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.graphics.Path;
import android.util.AttributeSet;
import android.view.View;

import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import androidx.core.content.ContextCompat;

/**
 * Compact, allocation-bounded traffic chart.
 *
 * <p>The view owns a fixed-size ring buffer. Samples are supplied by
 * {@link StatisticsFragment} from the router snapshot; the view itself never
 * calls native code and never performs I/O. Drawing is a simple canvas pass
 * over at most {@link #MAX_SAMPLES} points.</p>
 */
public class TrafficChartView extends View {
    public static final int MAX_SAMPLES = 120;

    private final long[] downloadHistory = new long[MAX_SAMPLES];
    private final long[] uploadHistory = new long[MAX_SAMPLES];
    private int sampleCount;
    private int nextIndex;

    private final Paint gridPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint downloadPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint uploadPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint downloadFillPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint uploadFillPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Path downloadPath = new Path();
    private final Path uploadPath = new Path();
    private final Path downloadFillPath = new Path();
    private final Path uploadFillPath = new Path();
    private final float density;

    public TrafficChartView(Context context) {
        this(context, null);
    }

    public TrafficChartView(Context context, @Nullable AttributeSet attrs) {
        this(context, attrs, 0);
    }

    public TrafficChartView(Context context, @Nullable AttributeSet attrs, int defStyleAttr) {
        super(context, attrs, defStyleAttr);
        density = getResources().getDisplayMetrics().density;
        setWillNotDraw(false);
        configurePaints();
    }

    private void configurePaints() {
        int gridColor = ContextCompat.getColor(getContext(), R.color.chart_grid);
        int downloadColor = ContextCompat.getColor(getContext(), R.color.chart_download);
        int uploadColor = ContextCompat.getColor(getContext(), R.color.chart_upload);

        gridPaint.setColor(gridColor);
        gridPaint.setStyle(Paint.Style.STROKE);
        gridPaint.setStrokeWidth(dp(1f));

        downloadPaint.setColor(downloadColor);
        downloadPaint.setStyle(Paint.Style.STROKE);
        downloadPaint.setStrokeWidth(dp(2f));
        downloadPaint.setStrokeCap(Paint.Cap.ROUND);
        downloadPaint.setStrokeJoin(Paint.Join.ROUND);

        uploadPaint.setColor(uploadColor);
        uploadPaint.setStyle(Paint.Style.STROKE);
        uploadPaint.setStrokeWidth(dp(2f));
        uploadPaint.setStrokeCap(Paint.Cap.ROUND);
        uploadPaint.setStrokeJoin(Paint.Join.ROUND);

        downloadFillPaint.setColor(downloadColor);
        downloadFillPaint.setStyle(Paint.Style.FILL);
        downloadFillPaint.setAlpha(38);

        uploadFillPaint.setColor(uploadColor);
        uploadFillPaint.setStyle(Paint.Style.FILL);
        uploadFillPaint.setAlpha(26);
    }

    /**
     * Add one sample. Negative values are clamped to zero. The oldest sample
     * is discarded once the ring is full.
     */
    public void addSample(long downloadBytesPerSecond, long uploadBytesPerSecond) {
        downloadHistory[nextIndex] = Math.max(0L, downloadBytesPerSecond);
        uploadHistory[nextIndex] = Math.max(0L, uploadBytesPerSecond);
        nextIndex = (nextIndex + 1) % MAX_SAMPLES;
        if (sampleCount < MAX_SAMPLES) {
            sampleCount++;
        }
        invalidate();
    }

    public void clear() {
        sampleCount = 0;
        nextIndex = 0;
        invalidate();
    }

    public int getSampleCount() {
        return sampleCount;
    }

    @NonNull
    public long[] copyDownloadHistory() {
        return chronologicalCopy(downloadHistory);
    }

    @NonNull
    public long[] copyUploadHistory() {
        return chronologicalCopy(uploadHistory);
    }

    public void restoreHistory(@Nullable long[] download, @Nullable long[] upload) {
        clear();
        if (download == null || upload == null) {
            return;
        }
        int length = Math.min(Math.min(download.length, upload.length), MAX_SAMPLES);
        for (int i = 0; i < length; i++) {
            addSample(download[i], upload[i]);
        }
    }

    @NonNull
    private long[] chronologicalCopy(@NonNull long[] source) {
        long[] result = new long[sampleCount];
        for (int i = 0; i < sampleCount; i++) {
            result[i] = source[indexAt(i)];
        }
        return result;
    }

    private int indexAt(int chronologicalIndex) {
        return (nextIndex - sampleCount + chronologicalIndex + MAX_SAMPLES * 2) % MAX_SAMPLES;
    }

    @Override
    protected void onDraw(@NonNull Canvas canvas) {
        super.onDraw(canvas);

        float left = getPaddingLeft();
        float top = getPaddingTop();
        float right = getWidth() - getPaddingRight();
        float bottom = getHeight() - getPaddingBottom();
        if (right <= left || bottom <= top) {
            return;
        }

        for (int line = 0; line <= 4; line++) {
            float y = top + (bottom - top) * line / 4f;
            canvas.drawLine(left, y, right, y, gridPaint);
        }

        if (sampleCount == 0) {
            return;
        }

        long maximum = 1024L;
        for (int i = 0; i < sampleCount; i++) {
            maximum = Math.max(maximum, downloadHistory[indexAt(i)]);
            maximum = Math.max(maximum, uploadHistory[indexAt(i)]);
        }

        buildPath(
                downloadPath, downloadFillPath, downloadHistory, maximum, left, top, right, bottom);
        buildPath(uploadPath, uploadFillPath, uploadHistory, maximum, left, top, right, bottom);

        canvas.drawPath(downloadFillPath, downloadFillPaint);
        canvas.drawPath(uploadFillPath, uploadFillPaint);
        canvas.drawPath(downloadPath, downloadPaint);
        canvas.drawPath(uploadPath, uploadPaint);
    }

    private void buildPath(@NonNull Path line, @NonNull Path fill, @NonNull long[] values,
            long maximum, float left, float top, float right, float bottom) {
        line.reset();
        fill.reset();

        int pointCount = Math.max(2, sampleCount);
        float plotWidth = right - left;
        float plotHeight = bottom - top;

        for (int point = 0; point < pointCount; point++) {
            int sample = sampleCount == 1 ? 0 : Math.min(point, sampleCount - 1);
            float x = left + plotWidth * point / (pointCount - 1f);
            float ratio = (float) Math.min(1.0d, values[indexAt(sample)] / (double) maximum);
            float y = bottom - plotHeight * ratio;
            if (point == 0) {
                line.moveTo(x, y);
                fill.moveTo(x, bottom);
                fill.lineTo(x, y);
            } else {
                line.lineTo(x, y);
                fill.lineTo(x, y);
            }
        }
        fill.lineTo(right, bottom);
        fill.close();
    }

    @NonNull
    @Override
    public CharSequence getAccessibilityClassName() {
        return TrafficChartView.class.getName();
    }

    @Override
    protected void onDetachedFromWindow() {
        clear();
        super.onDetachedFromWindow();
    }

    private float dp(float value) {
        return value * density;
    }
}
