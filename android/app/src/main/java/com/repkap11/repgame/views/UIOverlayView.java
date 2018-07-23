package com.repkap11.repgame.views;

import android.content.Context;
import android.content.res.TypedArray;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.support.annotation.Nullable;
import android.util.AttributeSet;
import android.view.MotionEvent;
import android.view.View;

import com.repkap11.repgame.R;
import com.repkap11.repgame.RepGameAndroidRenderer;

public class UIOverlayView extends View implements View.OnTouchListener {
    private static final String TAG = UIOverlayView.class.getSimpleName();

    private final Paint mMovePaint;
    private final float mMoveRadiusFraction;
    int mMovePointerId = -1;
    int mLookPointerId = -1;
    private RepGameAndroidRenderer mRenderWrapper;
    private int mMoveX;
    private int mMoveY;
    private int mMoveRadius;
    private int mMoveFingerX;
    private int mMoveFingerY;
    private int mMoveFingerRadius;
    private int mLookFingerX;
    private int mLookFingerY;

    public UIOverlayView(Context context, @Nullable AttributeSet attrs) {
        super(context, attrs);
        TypedArray a = context.getTheme().obtainStyledAttributes(attrs, R.styleable.UIOverlayView, 0, 0);
        int overlayColor = a.getColor(R.styleable.UIOverlayView_overlayColor, getResources().getColor(android.R.color.black));
        mMovePaint = new Paint(Paint.ANTI_ALIAS_FLAG);
        mMovePaint.setStyle(Paint.Style.FILL);
        mMovePaint.setColor(overlayColor);
        mMoveRadiusFraction = a.getFloat(R.styleable.UIOverlayView_moveRadiusFraction, 1.0f);
        setOnTouchListener(this);
    }

    @Override
    protected void onDraw(Canvas canvas) {
        super.onDraw(canvas);
        canvas.drawCircle(mMoveX, mMoveY, mMoveRadius, mMovePaint);
        canvas.drawCircle(mMoveFingerX, mMoveFingerY, mMoveFingerRadius, mMovePaint);
    }

    @Override
    protected void onSizeChanged(int w, int h, int oldw, int oldh) {
        int lookMaxRadius = Math.min(w / 3, h * 2 / 3) / 2;
        mMoveRadius = (int) ((float) lookMaxRadius * mMoveRadiusFraction);
        mMoveX = w - lookMaxRadius;
        mMoveY = h - lookMaxRadius;
        mMoveFingerX = mMoveX;
        mMoveFingerY = mMoveY;
        mMoveFingerRadius = mMoveRadius / 3;

        super.onSizeChanged(w, h, oldw, oldh);
    }

    public void setRenderer(RepGameAndroidRenderer renderer) {
        mRenderWrapper = renderer;
    }

    boolean eventWithinMove(MotionEvent event, int pointerIndex) {
        int x = (int) event.getX(pointerIndex);
        int y = (int) event.getY(pointerIndex);
        if ((Math.abs(x - mMoveX) < mMoveRadius) &&
                (Math.abs(y - mMoveY) < mMoveRadius)) {
            return true;
        }
        return false;
    }

    @Override
    public boolean onTouch(View view, MotionEvent event) {
        int actionIndex = event.getActionIndex();
        int actionPointer = event.getPointerId(actionIndex);
        switch (event.getActionMasked()) {
            case MotionEvent.ACTION_DOWN:
            case MotionEvent.ACTION_POINTER_DOWN:
                if (eventWithinMove(event, actionIndex)) {
                    //Log.e(TAG, "Event within look");
                    mMovePointerId = actionPointer;
                } else {
                    handleLook((int) event.getX(actionIndex), (int) event.getY(actionIndex), true);
                    mLookPointerId = actionPointer;
                    //Log.e(TAG, "Event NOT within look");
                }
                break;
            case MotionEvent.ACTION_UP:
            case MotionEvent.ACTION_CANCEL:
            case MotionEvent.ACTION_POINTER_UP:
                if (actionPointer == mMovePointerId) {
                    handleMove(mMoveX, mMoveY);
                    mMovePointerId = -1;
                }
                if (actionPointer == mLookPointerId) {
                    handleLook((int) mLookFingerX, mLookFingerY, false);
                    mLookPointerId = -1;
                }

                break;
            case MotionEvent.ACTION_MOVE:
                for (int i = 0; i < event.getPointerCount(); i++) {
                    if (event.getPointerId(i) == mMovePointerId) {
                        handleMove((int) event.getX(i), (int) event.getY(i));
                    }
                    if (event.getPointerId(i) == mLookPointerId) {
                        handleLook((int) event.getX(i), (int) event.getY(i), false);
                    }
                }
                break;
        }
        return true;
    }

    private void handleLook(int x, int y, boolean init) {
        if (!init) {
            mRenderWrapper.lookInput(x - mLookFingerX, y - mLookFingerY);
        }
        mLookFingerX = x;
        mLookFingerY = y;
    }


    private void handleMove(int x, int y) {
        int distX = Math.abs(mMoveX - x);
        int distY = Math.abs(mMoveY - y);
        double angle = Math.atan2(mMoveY - y, mMoveX - x);
        int maxDist = mMoveRadius - mMoveFingerRadius;
        int extraX = distX - maxDist;
        int extraY = distY - maxDist;
        if (extraX > 0) {
            distX -= extraX;
        }
        if (extraY > 0) {
            distY -= extraY;
        }
        int dist = (int) Math.sqrt((double) (distX * distX + distY * distY));

        //Log.e(TAG, "distX:" + distX + " distY:" + distY);
        //Log.e(TAG, "angle:" + Math.toDegrees(angle));

        x = (int) (mMoveX - Math.cos(angle) * distX);
        y = (int) (mMoveY - Math.sin(angle) * distY);
        if (x != mMoveFingerX || y != mMoveFingerY) {
            mRenderWrapper.positionInput((float) dist / (float) maxDist, 0, (float) (Math.toDegrees(angle)) - 90);
            invalidate();
            mMoveFingerX = x;
            mMoveFingerY = y;
        }


    }
}
