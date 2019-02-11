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
    int mUpPointerId = -1;
    int mDownPointerId = -1;
    int mMouseLeftPointerId = -1;
    int mMouseMiddlePointerId = -1;
    int mMouseRightPointerId = -1;
    private RepGameAndroidRenderer mRenderWrapper;
    private int mMoveX;
    private int mMoveY;
    private int mMoveRadius;
    private int mMoveFingerX;
    private int mMoveFingerY;
    private int mMoveFingerRadius;
    private int mLookFingerX;
    private int mLookFingerY;
    private int mUpRadius;
    private int mUpX;
    private int mUpX2;
    private int mUpY;
    private int mDownRadius;
    private int mDownX;
    private int mDownX2;
    private int mDownY;
    private int mMouseLeftX;
    private int mMouseLeftY;
    private int mMouseMiddleX;
    private int mMouseMiddleY;
    private int mMouseRightX;
    private int mMouseRightY;
    private int mMouseRadius;

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
        canvas.drawCircle(mUpX, mUpY, mUpRadius, mMovePaint);
        canvas.drawCircle(mUpX2, mUpY, mUpRadius, mMovePaint);
        canvas.drawCircle(mDownX, mDownY, mDownRadius, mMovePaint);
        canvas.drawCircle(mDownX2, mDownY, mDownRadius, mMovePaint);
        canvas.drawCircle(mMouseLeftX, mMouseLeftY, mMouseRadius, mMovePaint);
        canvas.drawCircle(mMouseMiddleX, mMouseMiddleY, mMouseRadius, mMovePaint);
        canvas.drawCircle(mMouseRightX, mMouseRightY, mMouseRadius, mMovePaint);
    }

    @Override
    protected void onSizeChanged(int w, int h, int oldw, int oldh) {
        int lookMaxRadius = Math.min(w / 4, h * 2 / 4) / 2;
        mMoveRadius = (int) ((float) lookMaxRadius * mMoveRadiusFraction);
        mMoveX = lookMaxRadius;
        mMoveY = h - lookMaxRadius;
        mMoveFingerX = mMoveX;
        mMoveFingerY = mMoveY;
        mMoveFingerRadius = mMoveRadius / 3;
        mUpRadius = mMoveRadius / 2;
        mUpX = lookMaxRadius / 2;
        mUpX2 = w - mUpX;

        mUpY = lookMaxRadius / 2;
        mDownRadius = mMoveRadius / 2;
        mDownX = lookMaxRadius / 2;
        mDownX2 = w - mDownX;
        mDownY = lookMaxRadius * 3 / 2;

        mMouseLeftY = h - mUpRadius;
        mMouseMiddleY = h - mUpRadius;
        mMouseRightY = h - mUpRadius;

        mMouseRadius = mMoveRadius / 2;
        mMouseLeftX = (int)(mMoveRadius * 2.5) + mMouseRadius;
        mMouseRightX = w - mMouseLeftX;
        mMouseMiddleX = (mMouseLeftX + mMouseRightX) / 2;


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

    boolean eventWithinUp(MotionEvent event, int pointerIndex) {
        int x = (int) event.getX(pointerIndex);
        int y = (int) event.getY(pointerIndex);
        if ((Math.abs(x - mUpX) < mUpRadius) &&
                (Math.abs(y - mUpY) < mUpRadius)) {
            return true;
        }
        if ((Math.abs(x - mUpX2) < mUpRadius) &&
                (Math.abs(y - mUpY) < mUpRadius)) {
            return true;
        }
        return false;
    }

    boolean eventWithinDown(MotionEvent event, int pointerIndex) {
        int x = (int) event.getX(pointerIndex);
        int y = (int) event.getY(pointerIndex);
        if ((Math.abs(x - mDownX) < mDownRadius) &&
                (Math.abs(y - mDownY) < mDownRadius)) {
            return true;
        }
        if ((Math.abs(x - mDownX2) < mDownRadius) &&
                (Math.abs(y - mDownY) < mDownRadius)) {
            return true;
        }
        return false;
    }

    private boolean eventWithinMouseLeft(MotionEvent event, int pointerIndex) {
        int x = (int) event.getX(pointerIndex);
        int y = (int) event.getY(pointerIndex);
        if ((Math.abs(x - mMouseLeftX) < mMouseRadius) &&
                (Math.abs(y - mMouseLeftY) < mMouseRadius)) {
            return true;
        }
        return false;
    }

    private boolean eventWithinMouseMiddle(MotionEvent event, int pointerIndex) {
        int x = (int) event.getX(pointerIndex);
        int y = (int) event.getY(pointerIndex);
        if ((Math.abs(x - mMouseMiddleX) < mMouseRadius) &&
                (Math.abs(y - mMouseMiddleY) < mMouseRadius)) {
            return true;
        }
        return false;
    }

    private boolean eventWithinMouseRight(MotionEvent event, int pointerIndex) {
        int x = (int) event.getX(pointerIndex);
        int y = (int) event.getY(pointerIndex);
        if ((Math.abs(x - mMouseRightX) < mMouseRadius) &&
                (Math.abs(y - mMouseRightY) < mMouseRadius)) {
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
                } else if (eventWithinUp(event, actionIndex)) {
                    mUpPointerId = actionPointer;
                    handleVMove();
                } else if (eventWithinMouseLeft(event, actionIndex)) {
                    mMouseLeftPointerId = actionPointer;
                    handleMouseButtons();
                } else if (eventWithinMouseMiddle(event, actionIndex)) {
                    mMouseMiddlePointerId = actionPointer;
                    handleMouseButtons();
                } else if (eventWithinMouseRight(event, actionIndex)) {
                    mMouseRightPointerId = actionPointer;
                    handleMouseButtons();
                } else if (eventWithinDown(event, actionIndex)) {
                    mDownPointerId = actionPointer;
                    handleVMove();
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
                    handleHMove(mMoveX, mMoveY);
                    mMovePointerId = -1;
                }
                if (actionPointer == mLookPointerId) {
                    handleLook((int) mLookFingerX, mLookFingerY, false);
                    mLookPointerId = -1;
                }
                if (actionPointer == mUpPointerId) {
                    mUpPointerId = -1;
                    handleVMove();
                }
                if (actionPointer == mDownPointerId) {
                    mDownPointerId = -1;
                    handleVMove();
                }
                if (actionPointer == mMouseLeftPointerId) {
                    mMouseLeftPointerId = -1;
                    handleMouseButtons();
                }
                if (actionPointer == mMouseMiddlePointerId) {
                    mMouseMiddlePointerId = -1;
                    handleMouseButtons();
                }
                if (actionPointer == mMouseRightPointerId) {
                    mMouseRightPointerId = -1;
                    handleMouseButtons();
                }

                break;
            case MotionEvent.ACTION_MOVE:
                for (int i = 0; i < event.getPointerCount(); i++) {
                    if (event.getPointerId(i) == mMovePointerId) {
                        handleHMove((int) event.getX(i), (int) event.getY(i));
                    }
                    if (event.getPointerId(i) == mLookPointerId) {
                        handleLook((int) event.getX(i), (int) event.getY(i), false);
                    }
                }
                break;
        }
        return true;
    }

    private void handleMouseButtons() {
        int left = mMouseLeftPointerId == -1 ? 0 : 1;
        int middle = mMouseMiddlePointerId == -1 ? 0 : 1;
        int right = mMouseRightPointerId == -1 ? 0 : 1;
        mRenderWrapper.setButtonState(left, middle, right);
    }

    private void handleVMove() {
        int size;
        if (mUpPointerId != -1 && mDownPointerId != -1) {
            //Up and down, dont move;
            size = 0;
        } else if (mUpPointerId != -1) {
            size = 1;
        } else if (mDownPointerId != -1) {
            size = -1;
        } else {
            size = 0;
        }
        mRenderWrapper.positionVInput(size);

    }

    private void handleLook(int x, int y, boolean init) {
        if (!init) {
            mRenderWrapper.lookInput(x - mLookFingerX, y - mLookFingerY);
        }
        mLookFingerX = x;
        mLookFingerY = y;
    }


    private void handleHMove(int x, int y) {
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
            mRenderWrapper.positionHInput((float) dist / (float) maxDist, (float) (Math.toDegrees(angle)) - 90);
            invalidate();
            mMoveFingerX = x;
            mMoveFingerY = y;
        }


    }
}
