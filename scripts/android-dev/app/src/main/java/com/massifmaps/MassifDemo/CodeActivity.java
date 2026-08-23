package com.massifmaps.MassifDemo;

import android.os.Bundle;
import android.view.View;
import android.widget.TextView;

import androidx.appcompat.app.AppCompatActivity;

import com.google.android.material.appbar.MaterialToolbar;
import com.massifmaps.MassifDemo.examples.Examples;

import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.InputStream;

/**
 * An example's own source, read straight out of the APK.
 *
 * The gradle build copies the examples package into the assets (see app/build.gradle), so what is
 * shown here is the file that ran - not a transcription that can drift from it. The website shows
 * the same file, read from the repo.
 */
public class CodeActivity extends AppCompatActivity {

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(R.layout.code_activity);

        Examples.Entry entry = Examples.byId(getIntent().getStringExtra(ExampleActivity.EXTRA_ID));
        MaterialToolbar toolbar = findViewById(R.id.toolbar);
        toolbar.setTitle(entry != null ? entry.title() : "");
        toolbar.setNavigationOnClickListener(new View.OnClickListener() {
            @Override
            public void onClick(View v) {
                finish();
            }
        });

        String source = entry != null ? read(entry.sourceAsset()) : null;
        ((TextView) findViewById(R.id.code))
            .setText(source != null ? source : getString(R.string.example_source_missing));
    }

    /** @return null when the sources were not bundled. */
    private String read(String asset) {
        InputStream stream = null;
        try {
            stream = getAssets().open(asset);
            ByteArrayOutputStream out = new ByteArrayOutputStream();
            byte[] buffer = new byte[8192];
            for (int read; (read = stream.read(buffer)) > 0; ) {
                out.write(buffer, 0, read);
            }
            return out.toString("UTF-8");
        } catch (IOException e) {
            return null;
        } finally {
            if (stream != null) {
                try {
                    stream.close();
                } catch (IOException ignored) {
                }
            }
        }
    }
}
