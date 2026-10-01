package com.massifmaps.MassifDemo.demo;

import android.graphics.Color;
import android.util.Log;
import android.util.TypedValue;
import android.view.ViewGroup;
import android.widget.TextView;
import androidx.constraintlayout.widget.ConstraintLayout;
import com.massifmaps.renderers.MapRendererListener;
import com.massifmaps.ui.MapView;

/**
 * Frames drawn per second and the GL-thread draw time, bottom-left and once a second in logcat
 * (tag 'fps'). WHEN_DIRTY draws only on request, so a still map reads 'idle'.
 */
public class FpsCounter extends MapRendererListener {
    private final TextView text;
    private long windowStart = System.nanoTime();
    private long lastFrame;
    private long frameStart;
    private long drawSum;
    private long drawMax;
    private int frames;

    private FpsCounter(TextView text) {
        this.text = text;
    }

    /** Keep the returned counter referenced: the native side holds the listener weakly. */
    public static FpsCounter attach(MapView mapView, ViewGroup root) {
        TextView text = new TextView(root.getContext());
        text.setBackgroundColor(0xA0000000);
        text.setTextColor(Color.WHITE);
        text.setTextSize(TypedValue.COMPLEX_UNIT_SP, 16);
        text.setPadding(12, 6, 12, 6);
        ConstraintLayout.LayoutParams params = new ConstraintLayout.LayoutParams(
            ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT);
        params.startToStart = ConstraintLayout.LayoutParams.PARENT_ID;
        params.bottomToBottom = ConstraintLayout.LayoutParams.PARENT_ID;
        params.bottomMargin = 48;
        root.addView(text, params);

        final FpsCounter counter = new FpsCounter(text);
        mapView.getMapRenderer().setMapRendererListener(counter);
        text.postDelayed(new Runnable() {
            public void run() {
                if (System.nanoTime() - counter.lastFrame > 1500000000L) {
                    counter.text.setText("idle");
                }
                counter.text.postDelayed(this, 500);
            }
        }, 500);
        return counter;
    }

    @Override
    public void onBeforeDrawFrame() {
        frameStart = System.nanoTime();
    }

    @Override
    public void onAfterDrawFrame() {
        long now = System.nanoTime();
        long draw = now - frameStart;
        if (now - lastFrame > 500000000L) {
            // first frame after a still map: the wait is not frame time
            windowStart = frameStart;
            drawSum = 0;
            drawMax = 0;
            frames = 0;
        }
        lastFrame = now;
        drawSum += draw;
        drawMax = Math.max(drawMax, draw);
        frames++;
        long elapsed = now - windowStart;
        if (elapsed < 1000000000L) {
            return;
        }
        final String line = String.format("%.1f fps  draw %.1f ms (max %.1f)",
                frames * 1e9 / elapsed, drawSum / 1e6 / frames, drawMax / 1e6);
        Log.i("fps", line);
        text.post(new Runnable() {
            public void run() {
                text.setText(line);
            }
        });
        windowStart = now;
        drawSum = 0;
        drawMax = 0;
        frames = 0;
    }
}
