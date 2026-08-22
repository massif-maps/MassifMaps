package com.massifmaps.MassifDemo.gallery;

import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.widget.ImageView;
import android.widget.TextView;

import androidx.annotation.NonNull;
import androidx.recyclerview.widget.GridLayoutManager;
import androidx.recyclerview.widget.RecyclerView;

import com.massifmaps.MassifDemo.R;
import com.massifmaps.MassifDemo.examples.Examples;
import com.massifmaps.MassifDemo.examples.Sections;

import java.io.IOException;
import java.io.InputStream;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

/**
 * The example grid: a full-width heading per section, then its cards.
 *
 * One flat list with two view types rather than nested lists - a section is a heading, not a
 * scrolling container, and nesting would break the single scroll the gallery is.
 */
public class GalleryAdapter extends RecyclerView.Adapter<RecyclerView.ViewHolder> {

    private static final int TYPE_HEADER = 0;
    private static final int TYPE_EXAMPLE = 1;

    /** A row: either a section heading or an example. */
    private static final class Row {
        final String sectionId;
        final Examples.Entry entry;

        Row(String sectionId, Examples.Entry entry) {
            this.sectionId = sectionId;
            this.entry = entry;
        }
    }

    public interface OnExampleClick {
        void onExample(Examples.Entry entry);
    }

    private final Context context;
    private final List<Row> rows = new ArrayList<>();
    private final OnExampleClick listener;
    /** Decoded screenshots, kept for the life of the screen - a few dozen small bitmaps. */
    private final Map<String, Bitmap> shots = new HashMap<>();

    public GalleryAdapter(Context context, OnExampleClick listener) {
        this.context = context;
        this.listener = listener;
        String previous = null;
        for (Examples.Entry entry : Examples.all()) {
            if (!entry.section().equals(previous)) {
                rows.add(new Row(entry.section(), null));
                previous = entry.section();
            }
            rows.add(new Row(entry.section(), entry));
        }
    }

    /** Headings span the whole width; examples take one cell. */
    public GridLayoutManager.SpanSizeLookup spanSizes(final int columns) {
        return new GridLayoutManager.SpanSizeLookup() {
            @Override
            public int getSpanSize(int position) {
                return rows.get(position).entry == null ? columns : 1;
            }
        };
    }

    @Override
    public int getItemViewType(int position) {
        return rows.get(position).entry == null ? TYPE_HEADER : TYPE_EXAMPLE;
    }

    @Override
    public int getItemCount() {
        return rows.size();
    }

    @NonNull
    @Override
    public RecyclerView.ViewHolder onCreateViewHolder(@NonNull ViewGroup parent, int viewType) {
        LayoutInflater inflater = LayoutInflater.from(parent.getContext());
        return viewType == TYPE_HEADER
            ? new HeaderHolder(inflater.inflate(R.layout.gallery_header, parent, false))
            : new ExampleHolder(inflater.inflate(R.layout.gallery_item, parent, false));
    }

    @Override
    public void onBindViewHolder(@NonNull RecyclerView.ViewHolder holder, int position) {
        Row row = rows.get(position);
        if (holder instanceof HeaderHolder) {
            ((HeaderHolder) holder).bind(row.sectionId);
        } else {
            ((ExampleHolder) holder).bind(row.entry);
        }
    }

    private static final class HeaderHolder extends RecyclerView.ViewHolder {
        private final TextView title;
        private final TextView description;

        HeaderHolder(View view) {
            super(view);
            title = view.findViewById(R.id.sectionTitle);
            description = view.findViewById(R.id.sectionDescription);
        }

        void bind(String sectionId) {
            title.setText(Sections.titleOf(sectionId));
            String blurb = null;
            for (String[] section : Sections.ALL) {
                if (section[0].equals(sectionId)) {
                    blurb = section[2];
                }
            }
            description.setText(blurb == null ? "" : blurb);
            description.setVisibility(blurb == null ? View.GONE : View.VISIBLE);
        }
    }

    private final class ExampleHolder extends RecyclerView.ViewHolder {
        private final ImageView shot;
        private final TextView title;
        private final TextView description;

        ExampleHolder(View view) {
            super(view);
            shot = view.findViewById(R.id.shot);
            title = view.findViewById(R.id.title);
            description = view.findViewById(R.id.description);
        }

        void bind(final Examples.Entry entry) {
            title.setText(entry.title());
            description.setText(entry.description());
            Bitmap bitmap = screenshot(entry.shotAsset());
            shot.setImageBitmap(bitmap);
            // No screenshot yet: the placeholder colour is the card's own, so a missing capture
            // reads as "not captured" rather than as a broken image.
            shot.setVisibility(bitmap == null ? View.GONE : View.VISIBLE);
            itemView.setOnClickListener(new View.OnClickListener() {
                @Override
                public void onClick(View v) {
                    listener.onExample(entry);
                }
            });
        }
    }

    /** @return null when the example has no capture in docs/examples/screenshots yet. */
    private Bitmap screenshot(String asset) {
        if (shots.containsKey(asset)) {
            return shots.get(asset);
        }
        Bitmap bitmap = null;
        InputStream stream = null;
        try {
            stream = context.getAssets().open(asset);
            bitmap = BitmapFactory.decodeStream(stream);
        } catch (IOException ignored) {
            // Expected for a new example.
        } finally {
            close(stream);
        }
        shots.put(asset, bitmap);
        return bitmap;
    }

    private static void close(InputStream stream) {
        if (stream != null) {
            try {
                stream.close();
            } catch (IOException ignored) {
            }
        }
    }
}
