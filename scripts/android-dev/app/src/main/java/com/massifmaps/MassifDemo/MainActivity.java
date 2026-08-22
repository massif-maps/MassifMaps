package com.massifmaps.MassifDemo;

import android.content.Intent;
import android.os.Bundle;
import android.view.MenuItem;
import android.view.View;
import android.widget.TextView;

import androidx.annotation.NonNull;
import androidx.appcompat.app.AppCompatActivity;
import androidx.core.graphics.Insets;
import androidx.core.view.ViewCompat;
import androidx.core.view.WindowCompat;
import androidx.core.view.WindowInsetsCompat;
import androidx.recyclerview.widget.GridLayoutManager;
import androidx.recyclerview.widget.RecyclerView;

import com.google.android.material.appbar.MaterialToolbar;
import com.massifmaps.MassifDemo.examples.Examples;
import com.massifmaps.MassifDemo.gallery.GalleryAdapter;

/**
 * The example gallery - what the app opens on.
 *
 * The list is generated from the examples' own @ExampleInfo annotations
 * (scripts/gen-examples.py), so adding an example file is the whole job.
 *
 * The debugging/benchmark map is {@link BenchActivity}, reached from the toolbar or directly:
 *   adb shell am start -n com.massifmaps.MassifDemo/.BenchActivity
 */
public class MainActivity extends AppCompatActivity implements GalleryAdapter.OnExampleClick {

    /** A card wants about this much width to keep its screenshot readable. */
    private static final int CARD_WIDTH_DP = 190;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        WindowCompat.setDecorFitsSystemWindows(getWindow(), false);
        setContentView(R.layout.gallery_activity);

        MaterialToolbar toolbar = findViewById(R.id.toolbar);
        toolbar.setOnMenuItemClickListener(new MaterialToolbar.OnMenuItemClickListener() {
            @Override
            public boolean onMenuItemClick(@NonNull MenuItem item) {
                if (item.getItemId() == R.id.action_bench) {
                    startActivity(new Intent(MainActivity.this, BenchActivity.class));
                    return true;
                }
                return false;
            }
        });

        RecyclerView list = findViewById(R.id.exampleList);
        int columns = Math.max(1, (int) (getResources().getConfiguration().screenWidthDp
                                         / (float) CARD_WIDTH_DP));
        GalleryAdapter adapter = new GalleryAdapter(this, this);
        GridLayoutManager layout = new GridLayoutManager(this, columns);
        layout.setSpanSizeLookup(adapter.spanSizes(columns));
        list.setLayoutManager(layout);
        list.setAdapter(adapter);

        if (Examples.all().isEmpty()) {
            TextView empty = new TextView(this);
            empty.setText(R.string.gallery_empty);
            empty.setPadding(32, 32, 32, 32);
            ((android.view.ViewGroup) findViewById(R.id.galleryRoot)).addView(empty);
        }
        // The bar, not the toolbar: the toolbar has a fixed height and padding would squash it.
        applyInsets(findViewById(R.id.appBar), list);
    }

    @Override
    public void onExample(Examples.Entry entry) {
        startActivity(new Intent(this, ExampleActivity.class)
                          .putExtra(ExampleActivity.EXTRA_ID, entry.id()));
    }

    /** Edge to edge: the bar clears the status bar, the list clears the navigation bar. */
    private void applyInsets(final View appBar, final View list) {
        ViewCompat.setOnApplyWindowInsetsListener(findViewById(R.id.galleryRoot),
            new androidx.core.view.OnApplyWindowInsetsListener() {
                @Override
                public WindowInsetsCompat onApplyWindowInsets(View view, WindowInsetsCompat insets) {
                    Insets bars = insets.getInsets(WindowInsetsCompat.Type.systemBars());
                    appBar.setPadding(0, bars.top, 0, 0);
                    list.setPadding(list.getPaddingLeft(), list.getPaddingTop(),
                                    list.getPaddingRight(), bars.bottom + 24);
                    return insets;
                }
            });
    }
}
