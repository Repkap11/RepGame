package com.repkap11.repgame.views;

import android.content.Context;
import android.content.res.TypedArray;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.support.annotation.Nullable;
import android.util.AttributeSet;
import android.util.Log;
import android.view.MotionEvent;
import android.view.View;

import com.repkap11.repgame.R;
import com.repkap11.repgame.RepGameAndroidRenderer;

public class UIOverlayView extends View implements View.OnTouchListener {
    private static final String TAG = UIOverlayView.class.getSimpleName();

    private final Paint mLookPaint;
    private final float mLookRadiusFraction;
    private RepGameAndroidRenderer mRenderWrapper;
    private int mLookX;
    private int mLookY;
    private int mLookRadius;
    private int mLookFingerX;
    private int mLookFingerY;
    private int mLookFingerRadius;

    public UIOverlayView(Context context, @Nullable AttributeSet attrs) {
        super(context, attrs);
        TypedArray a = context.getTheme().obtainStyledAttributes(attrs, R.styleable.UIOverlayView, 0, 0);
        int overlayColor = a.getColor(R.styleable.UIOverlayView_overlayColor, getResources().getColor(android.R.color.black));
        mLookPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
        mLookPaint.setStyle(Paint.Style.FILL);
        mLookPaint.setColor(overlayColor);
        mLookRadiusFraction = a.getFloat(R.styleable.UIOverlayView_lookRadiusFraction, 1.0f);
        setOnTouchListener(this);
    }

    @Override
    protected void onDraw(Canvas canvas) {
        super.onDraw(canvas);
        canvas.drawCircle(mLookX, mLookY, mLookRadius, mLookPaint);
        canvas.drawCircle(mLookFingerX, mLookFingerY, mLookFingerRadius, mLookPaint);
    }

    @Override
    protected void onSizeChanged(int w, int h, int oldw, int oldh) {
        int halfWidth = w / 2;
        int halfHeight = h / 2;
        mLookX = halfWidth / 2;
        mLookY = halfHeight;
        int lookMaxRadius = Math.min(h, halfWidth) / 2;
        mLookRadius = (int) ((float) lookMaxRadius * mLookRadiusFraction);

        mLookFingerX = mLookX;
        mLookFingerY = mLookY;
        mLookFingerRadius = mLookRadius / 4;

        super.onSizeChanged(w, h, oldw, oldh);
    }

    public void setRenderer(RepGameAndroidRenderer renderer) {
        mRenderWrapper = renderer;
    }

    @Override
    public boolean onTouch(View view, MotionEvent event) {
        int x = (int) event.getX();
        int y = (int) event.getY();
        switch (event.getAction()) {
            case MotionEvent.ACTION_UP:
            case MotionEvent.ACTION_DOWN:
            case MotionEvent.ACTION_CANCEL:
            case MotionEvent.ACTION_POINTER_DOWN:
            case MotionEvent.ACTION_POINTER_UP:
                //handleLook(mLookX, mLookY);
                //mRenderWrapper.onMouseInput(0, 0);
                break;
            case MotionEvent.ACTION_MOVE:
                //handleTouch(x, y);
                //handleLookMove(x, y);
                break;
        }
        return true;
    }
    private void handleLook(int x, int y) {
        mRenderWrapper.onLookInput(x - mLookFingerX, y - mLookFingerY);
        invalidate();
    }


    private void handleMove(int x, int y) {
        int distX = Math.abs(mLookX - x);
        int distY = Math.abs(mLookY - y);
        double angle = Math.atan2(mLookY - y, mLookX - x);
        int extraX = distX - (mLookRadius - mLookFingerRadius);
        int extraY = distY - (mLookRadius - mLookFingerRadius);
        if (extraX > 0) {
            distX -= extraX;
        }
        if (extraY > 0) {
            distY -= extraY;
        }
        //Log.e(TAG, "distX:" + distX + " distY:" + distY);
        //Log.e(TAG, "angle:" + Math.toDegrees(angle));

        x = (int) (mLookX - Math.cos(angle) * distX);
        y = (int) (mLookY - Math.sin(angle) * distY);
        if (x != mLookFingerX || y != mLookFingerY) {
            mRenderWrapper.onMoveInput(x - mLookFingerX, y - mLookFingerY);
            invalidate();
        }

        mLookFingerX = x;
        mLookFingerY = y;

    }
}
