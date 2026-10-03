package com.repkap11.repgame.views;

import android.content.Context;
import android.content.res.TypedArray;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.support.annotation.Nullable;
import android.util.AttributeSet;
import android.view.MotionEvent;
import android.view.View;
import android.view.ViewConfiguration;

import com.repkap11.repgame.R;
import com.repkap11.repgame.RepGameAndroidRenderer;

public class UIOverlayView extends View implements View.OnTouchListener {
    private static final String TAG = UIOverlayView.class.getSimpleName();
    // The game renders the hotbar across the bottom of the screen, sized as a
    // fraction of a 16:9-normalized height (see InventoryRenderer::onSizeChange),
    // so it grows past 10% of the real height on wide displays. All buttons are
    // placed above that strip so hotbar slots stay tappable.
    private static final float HOTBAR_MAX_HEIGHT_PERCENT = 0.10f;

    private final Paint mButtonPaint;
    private final Paint mPressedPaint;
    private final Paint mTextPaint;
    private final float mMoveRadiusFraction;
    private final int mTouchSlop;
    int mMovePointerId = -1;
    int mLookPointerId = -1;
    int mJumpPointerId = -1;
    // Sneak is a toggle on touch (holding it while aiming is impractical).
    boolean mSneakOn = false;
    int mMouseLeftPointerId = -1;
    int mMouseMiddlePointerId = -1;
    int mMouseRightPointerId = -1;
    int mInventoryTouchPointerId = -1;
    private boolean mInventoryOpen = false;
    private RepGameAndroidRenderer mRenderWrapper;
    private int mMoveX;
    private int mMoveY;
    private int mMoveRadius;
    private int mMoveFingerX;
    private int mMoveFingerY;
    private int mMoveFingerRadius;
    private int mLookDownX;
    private int mLookDownY;
    private int mLookFingerX;
    private int mLookFingerY;
    private int mJumpRadius;
    private int mJumpX;
    private int mJumpY;
    private int mSneakX;
    private int mSneakY;
    private int mMouseLeftX;
    private int mMouseLeftY;
    private int mMouseMiddleX;
    private int mMouseMiddleY;
    private int mMouseRightX;
    private int mMouseRightY;
    private int mMouseRadius;
    private int mInventoryX;
    private int mInventoryY;
    private int mInventoryRadius;
    private int mInventoryDownX;
    private int mInventoryDownY;
    private int mModeX;
    private int mModeY;
    private int mModeRadius;
    private int mHotbarTop;

    public UIOverlayView(Context context, @Nullable AttributeSet attrs) {
        super(context, attrs);
        TypedArray a = context.getTheme().obtainStyledAttributes(attrs, R.styleable.UIOverlayView, 0, 0);
        int overlayColor = a.getColor(R.styleable.UIOverlayView_overlayColor, getResources().getColor(android.R.color.black));
        mButtonPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
        mButtonPaint.setStyle(Paint.Style.FILL);
        mButtonPaint.setColor(overlayColor);
        mPressedPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
        mPressedPaint.setStyle(Paint.Style.FILL);
        mPressedPaint.setColor((overlayColor & 0x00FFFFFF) | 0xB0000000);
        mTextPaint = new Paint(Paint.ANTI_ALIAS_FLAG);
        mTextPaint.setColor(0xE0FFFFFF);
        mTextPaint.setTextAlign(Paint.Align.CENTER);
        mMoveRadiusFraction = a.getFloat(R.styleable.UIOverlayView_moveRadiusFraction, 1.0f);
        a.recycle();
        mTouchSlop = ViewConfiguration.get(context).getScaledTouchSlop();
        setOnTouchListener(this);
    }

    private void drawButton(Canvas canvas, int cx, int cy, int radius, String label, boolean pressed) {
        canvas.drawCircle(cx, cy, radius, pressed ? mPressedPaint : mButtonPaint);
        float textY = cy - (mTextPaint.descent() + mTextPaint.ascent()) / 2.0f;
        canvas.drawText(label, cx, textY, mTextPaint);
    }

    @Override
    protected void onDraw(Canvas canvas) {
        super.onDraw(canvas);
        if (mInventoryOpen) {
            drawButton(canvas, mInventoryX, mInventoryY, mInventoryRadius, "Close", false);
            String mode = mRenderWrapper.getGameMode() == 0 ? "Creative" : "Survival";
            drawButton(canvas, mModeX, mModeY, mModeRadius, mode, false);
            return;
        }
        canvas.drawCircle(mMoveX, mMoveY, mMoveRadius, mButtonPaint);
        canvas.drawCircle(mMoveFingerX, mMoveFingerY, mMoveFingerRadius, mMovePointerId != -1 ? mPressedPaint : mButtonPaint);
        drawButton(canvas, mJumpX, mJumpY, mJumpRadius, "Jump", mJumpPointerId != -1);
        drawButton(canvas, mSneakX, mSneakY, mJumpRadius, "Sneak", mSneakOn);
        drawButton(canvas, mMouseLeftX, mMouseLeftY, mMouseRadius, "Break", mMouseLeftPointerId != -1);
        drawButton(canvas, mMouseMiddleX, mMouseMiddleY, mMouseRadius, "Pick", mMouseMiddlePointerId != -1);
        drawButton(canvas, mMouseRightX, mMouseRightY, mMouseRadius, "Place", mMouseRightPointerId != -1);
        drawButton(canvas, mInventoryX, mInventoryY, mInventoryRadius, "Inv", false);
    }

    @Override
    protected void onSizeChanged(int w, int h, int oldw, int oldh) {
        int lookMaxRadius = Math.min(w / 4, h * 2 / 4) / 2;
        mMoveRadius = (int) ((float) lookMaxRadius * mMoveRadiusFraction);
        float uiHeightBasis = Math.max(h, w * 9.0f / 16.0f);
        mHotbarTop = h - (int) (Math.min(uiHeightBasis * HOTBAR_MAX_HEIGHT_PERCENT, h) * 1.05f);

        // Joystick sits just above the hotbar strip so it doesn't cover slots.
        mMoveX = lookMaxRadius;
        mMoveY = mHotbarTop - mMoveRadius;
        mMoveFingerX = mMoveX;
        mMoveFingerY = mMoveY;
        mMoveFingerRadius = mMoveRadius / 3;

        mJumpRadius = mMoveRadius / 2;
        mJumpX = w - lookMaxRadius / 2;
        mJumpY = lookMaxRadius / 2;
        mSneakX = lookMaxRadius / 2;
        mSneakY = mJumpY;

        mMouseRadius = mMoveRadius / 2;
        mMouseLeftY = mHotbarTop - mMouseRadius;
        mMouseMiddleY = mMouseLeftY;
        mMouseRightY = mMouseLeftY;
        mMouseLeftX = (int) (mMoveRadius * 2.8) + mMouseRadius;
        mMouseRightX = w - mMouseLeftX;
        mMouseMiddleX = (mMouseLeftX + mMouseRightX) / 2;

        mInventoryRadius = lookMaxRadius / 2;
        mInventoryX = w - mInventoryRadius;
        mInventoryY = mHotbarTop - mInventoryRadius;

        // Shown only while the inventory is open; takes the jump slot.
        mModeRadius = mJumpRadius;
        mModeX = mJumpX;
        mModeY = mJumpY;

        mTextPaint.setTextSize(mMouseRadius * 0.45f);

        super.onSizeChanged(w, h, oldw, oldh);
    }

    public void setRenderer(RepGameAndroidRenderer renderer) {
        mRenderWrapper = renderer;
    }

    public boolean isInventoryOpen() {
        return mInventoryOpen;
    }

    public void closeInventory() {
        if (mInventoryOpen) {
            handleInventoryButton();
        }
    }

    private boolean withinCircle(MotionEvent event, int pointerIndex, int cx, int cy, int radius) {
        int x = (int) event.getX(pointerIndex);
        int y = (int) event.getY(pointerIndex);
        int dx = x - cx;
        int dy = y - cy;
        return dx * dx + dy * dy <= radius * radius;
    }

    @Override
    public boolean onTouch(View view, MotionEvent event) {
        int actionIndex = event.getActionIndex();
        int actionPointer = event.getPointerId(actionIndex);
        switch (event.getActionMasked()) {
            case MotionEvent.ACTION_DOWN:
            case MotionEvent.ACTION_POINTER_DOWN:
                if (mInventoryOpen) {
                    if (withinCircle(event, actionIndex, mInventoryX, mInventoryY, mInventoryRadius)) {
                        handleInventoryButton();
                    } else if (withinCircle(event, actionIndex, mModeX, mModeY, mModeRadius)) {
                        mRenderWrapper.toggleGameMode();
                        // The game applies the toggle on its next tick; redraw
                        // shortly after so the label shows the new mode.
                        postInvalidateDelayed(150);
                    } else {
                        mInventoryTouchPointerId = actionPointer;
                        mInventoryDownX = (int) event.getX(actionIndex);
                        mInventoryDownY = (int) event.getY(actionIndex);
                        mRenderWrapper.setMousePosition(mInventoryDownX, mInventoryDownY);
                    }
                    break;
                }
                if (withinCircle(event, actionIndex, mMoveX, mMoveY, mMoveRadius)) {
                    mMovePointerId = actionPointer;
                } else if (withinCircle(event, actionIndex, mJumpX, mJumpY, mJumpRadius)) {
                    mJumpPointerId = actionPointer;
                    handleJump();
                } else if (withinCircle(event, actionIndex, mSneakX, mSneakY, mJumpRadius)) {
                    mSneakOn = !mSneakOn;
                    mRenderWrapper.setSneakPressed(mSneakOn ? 1 : 0);
                } else if (withinCircle(event, actionIndex, mMouseLeftX, mMouseLeftY, mMouseRadius)) {
                    mMouseLeftPointerId = actionPointer;
                    handleMouseButtons();
                } else if (withinCircle(event, actionIndex, mMouseMiddleX, mMouseMiddleY, mMouseRadius)) {
                    mMouseMiddlePointerId = actionPointer;
                    handleMouseButtons();
                } else if (withinCircle(event, actionIndex, mMouseRightX, mMouseRightY, mMouseRadius)) {
                    mMouseRightPointerId = actionPointer;
                    handleMouseButtons();
                } else if (withinCircle(event, actionIndex, mInventoryX, mInventoryY, mInventoryRadius)) {
                    handleInventoryButton();
                } else {
                    mLookPointerId = actionPointer;
                    mLookDownX = (int) event.getX(actionIndex);
                    mLookDownY = (int) event.getY(actionIndex);
                    handleLook(mLookDownX, mLookDownY, true);
                }
                invalidate();
                break;
            case MotionEvent.ACTION_UP:
            case MotionEvent.ACTION_CANCEL:
            case MotionEvent.ACTION_POINTER_UP:
                if (actionPointer == mMovePointerId) {
                    handleHMove(mMoveX, mMoveY);
                    mMovePointerId = -1;
                }
                if (actionPointer == mLookPointerId) {
                    int dx = (int) event.getX(actionIndex) - mLookDownX;
                    int dy = (int) event.getY(actionIndex) - mLookDownY;
                    if (dx * dx + dy * dy <= mTouchSlop * mTouchSlop) {
                        mRenderWrapper.screenTapped((int) event.getX(actionIndex), (int) event.getY(actionIndex));
                    }
                    handleLook(mLookFingerX, mLookFingerY, false);
                    mLookPointerId = -1;
                }
                if (actionPointer == mJumpPointerId) {
                    mJumpPointerId = -1;
                    handleJump();
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
                if (actionPointer == mInventoryTouchPointerId) {
                    int dx = (int) event.getX(actionIndex) - mInventoryDownX;
                    int dy = (int) event.getY(actionIndex) - mInventoryDownY;
                    if (dx * dx + dy * dy <= mTouchSlop * mTouchSlop) {
                        mRenderWrapper.screenTapped((int) event.getX(actionIndex), (int) event.getY(actionIndex));
                    }
                    mInventoryTouchPointerId = -1;
                }
                invalidate();
                break;
            case MotionEvent.ACTION_MOVE:
                for (int i = 0; i < event.getPointerCount(); i++) {
                    int pointerId = event.getPointerId(i);
                    if (pointerId == mMovePointerId) {
                        handleHMove((int) event.getX(i), (int) event.getY(i));
                    }
                    if (pointerId == mLookPointerId) {
                        handleLook((int) event.getX(i), (int) event.getY(i), false);
                    }
                    if (pointerId == mInventoryTouchPointerId) {
                        mRenderWrapper.setMousePosition((int) event.getX(i), (int) event.getY(i));
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

    private void handleInventoryButton() {
        mInventoryOpen = !mInventoryOpen;
        mRenderWrapper.onInventoryClicked();
        invalidate();
    }

    private void handleJump() {
        mRenderWrapper.setJumpPressed(mJumpPointerId != -1 ? 1 : 0);
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
