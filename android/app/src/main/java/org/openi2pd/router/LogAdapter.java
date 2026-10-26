/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 *
 * openi2pd: LogAdapter.
 *
 * RecyclerView adapter for log rows with sequence-based diffing, local time
 * formatting and severity colouring derived from the level text. It is
 * read-only: message text is already sanitised and bounded by {@link LogEntry}
 * and the native ring, and row-level content descriptions are avoided to keep
 * TalkBack output usable.
 *
 */

package org.openi2pd.router;

import android.content.Context;
import android.text.TextUtils;
import android.text.format.DateFormat;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.widget.TextView;

import androidx.annotation.NonNull;
import androidx.core.content.ContextCompat;
import androidx.recyclerview.widget.DiffUtil;
import androidx.recyclerview.widget.ListAdapter;
import androidx.recyclerview.widget.RecyclerView;

import com.google.android.material.color.MaterialColors;

import java.util.Date;
import java.util.Locale;
import java.util.Objects;

/**
 * Recycled log list. The adapter is read-only: log text is sanitised by the
 * native logger and is only formatted for display here.
 */
public class LogAdapter extends ListAdapter<LogEntry, LogAdapter.Holder> {
    private static final int MAX_LEVEL_LENGTH = 12;

    private static final DiffUtil.ItemCallback<LogEntry> DIFF = new DiffUtil.ItemCallback<>() {
        @Override
        public boolean areItemsTheSame(@NonNull LogEntry oldItem, @NonNull LogEntry newItem) {
            return oldItem.sequence() == newItem.sequence();
        }

        @Override
        public boolean areContentsTheSame(@NonNull LogEntry oldItem, @NonNull LogEntry newItem) {
            return oldItem.sequence() == newItem.sequence()
                    && oldItem.timestampSeconds() == newItem.timestampSeconds()
                    && Objects.equals(oldItem.level(), newItem.level())
                    && Objects.equals(oldItem.message(), newItem.message());
        }
    };

    private final java.text.DateFormat timeFormat;
    private final int colorError;
    private final int colorWarn;
    private final int colorInfo;
    private final int colorDebug;

    public LogAdapter(@NonNull Context context) {
        super(DIFF);
        timeFormat = DateFormat.getTimeFormat(context);
        colorError = MaterialColors.getColor(context, androidx.appcompat.R.attr.colorError,
                ContextCompat.getColor(context, R.color.brand_error));
        colorWarn = ContextCompat.getColor(context, R.color.brand_warning);
        colorInfo = MaterialColors.getColor(context, androidx.appcompat.R.attr.colorPrimary,
                ContextCompat.getColor(context, R.color.brand_primary));
        colorDebug = MaterialColors.getColor(context,
                com.google.android.material.R.attr.colorOnSurfaceVariant,
                ContextCompat.getColor(context, R.color.brand_on_surface_variant));
    }

    @NonNull
    @Override
    public Holder onCreateViewHolder(@NonNull ViewGroup parent, int viewType) {
        View view =
                LayoutInflater.from(parent.getContext()).inflate(R.layout.item_log, parent, false);
        return new Holder(view);
    }

    @Override
    public void onBindViewHolder(@NonNull Holder holder, int position) {
        holder.bind(getItem(position));
    }

    final class Holder extends RecyclerView.ViewHolder {
        private final TextView time;
        private final TextView level;
        private final TextView message;

        Holder(@NonNull View itemView) {
            super(itemView);
            time = itemView.findViewById(R.id.log_time);
            level = itemView.findViewById(R.id.log_level);
            message = itemView.findViewById(R.id.log_message);
        }

        void bind(@NonNull LogEntry entry) {
            Context context = itemView.getContext();

            String timeText = entry.timestampSeconds() > 0L
                    ? timeFormat.format(new Date(entry.timestampSeconds() * 1000L))
                    : context.getString(R.string.value_unknown);
            String rawLevel = entry.level() == null ? "" : entry.level().trim();
            String levelText = rawLevel.isEmpty() ? context.getString(R.string.value_unknown)
                                                  : rawLevel.toUpperCase(Locale.ROOT);
            if (levelText.length() > MAX_LEVEL_LENGTH) {
                levelText = levelText.substring(0, MAX_LEVEL_LENGTH);
            }
            String messageText = TextUtils.isEmpty(entry.message())
                    ? context.getString(R.string.value_unknown)
                    : entry.message();

            time.setText(timeText);
            level.setText(levelText);
            message.setText(messageText);
            level.setTextColor(colorFor(rawLevel));
            // The row is one screen-reader stop. The child TextViews are
            // excluded from the accessibility tree (item_log.xml) so TalkBack
            // does not announce the timestamp, level and message separately,
            // and the combined description preserves the reading order.
            itemView.setContentDescription(context.getString(
                    R.string.log_row_description, timeText, levelText, messageText));
        }

        private int colorFor(@NonNull String rawLevel) {
            String value = rawLevel.toLowerCase(Locale.ROOT);
            if (value.contains("error") || value.contains("crit") || value.contains("fatal")
                    || value.contains("fail")) {
                return colorError;
            }
            if (value.contains("warn")) {
                return colorWarn;
            }
            if (value.contains("debug") || value.contains("trace")) {
                return colorDebug;
            }
            return colorInfo;
        }
    }
}
