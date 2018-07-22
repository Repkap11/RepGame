package com.repkap11.repgame.views;

import android.content.Context;
import android.content.res.TypedArray;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.support.annotation.Nullable;
import android.util.AttributeSet;
import android.util.Log;
import android.view.View;

import com.repkap11.repgame.R;

public class UIOverlayView extends View {
    private static final String TAG = UIOverlayView.class.getSimpleName();
    private final Paint mLookPaint;
    private int mWidth;
    private int mHeight;
    private int mHalfWidth;

    public UIOverlayView(Context context, @Nullable AttributeSet attrs) {
        super(context, attrs);
        TypedArray a = context.getTheme().obtainStyledAttributes(attrs, R.styleable.UIOverlayView, 0, 0);
        int overlayColor = a.getColor(R.styleable.UIOverlayView_overlayColor, getResources().getColor(android.R.color.black));
        setBackgroundColor(overlayColor);
        mLookPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
        mLookPaint.setStyle(Paint.Style.FILL);
        mLookPaint.setColor(overlayColor);
    }

    @Override
    protected void onDraw(Canvas canvas) {
        super.onDraw(canvas);
    }

    //    @Override
//    protected void onMeasure(int widthMeasureSpec, int heightMeasureSpec) {
//        Log.v(TAG, "Width :" + MeasureSpec.toString(widthMeasureSpec));
//        Log.v(TAG, "Height:" + MeasureSpec.toString(heightMeasureSpec));
//
//        int desiredWidth = getSuggestedMinimumWidth() + getPaddingLeft() + getPaddingRight();
//        int desiredHeight = getSuggestedMinimumHeight() + getPaddingTop() + getPaddingBottom();
//
//        setMeasuredDimension(measureDimension(desiredWidth, widthMeasureSpec), measureDimension(desiredHeight, heightMeasureSpec));
//    }
//
//    private int measureDimension(int desiredSize, int measureSpec) {
//        int result;
//        int specMode = MeasureSpec.getMode(measureSpec);
//        int specSize = MeasureSpec.getSize(measureSpec);
//
//        if (specMode == MeasureSpec.EXACTLY) {
//            result = specSize;
//        } else {
//            result = desiredSize;
//            if (specMode == MeasureSpec.AT_MOST) {
//                result = Math.min(result, specSize);
//            }
//        }
//
//        if (result < desiredSize) {
//            Log.e(TAG, "The view is too small, the content might get cut");
//        }
//        return result;
//    }
}
