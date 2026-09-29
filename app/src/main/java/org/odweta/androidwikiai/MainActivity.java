package org.odweta.androidwikiai;

import android.app.Activity;
import android.content.Intent;
import android.database.Cursor;
import android.net.Uri;
import android.os.Bundle;
import android.provider.OpenableColumns;
import android.view.Gravity;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.EditText;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;
import android.widget.Toast;

import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.nio.file.Files;
import java.nio.file.StandardCopyOption;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

public final class MainActivity extends Activity {
    private static final int REQUEST_MODEL = 1;
    private static final int REQUEST_WIKIPEDIA = 2;
    private static final String MODEL_FILE = "assistant.gguf";
    private static final String WIKIPEDIA_FILE = "wikipedia.zim";

    private final ExecutorService importExecutor = Executors.newSingleThreadExecutor();
    private TextView modelStatus;
    private TextView wikipediaStatus;
    private TextView answer;
    private Button modelButton;
    private Button wikipediaButton;

    @Override
    public void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        render();
        refreshResourceStatus();
    }

    private void render() {
        int padding = dp(24);
        LinearLayout content = new LinearLayout(this);
        content.setOrientation(LinearLayout.VERTICAL);
        content.setPadding(padding, padding, padding, padding);

        TextView title = text("Offline Wiki Assistant", 26, true);
        content.addView(title, matchWrap());
        content.addView(text(
                "Your files stay on this device. Download files separately, then import them here.",
                16, false), margin(matchWrap(), 8, 0, 0, 20));

        content.addView(text("Local AI model (.gguf)", 18, true), matchWrap());
        modelStatus = text("", 14, false);
        content.addView(modelStatus, margin(matchWrap(), 4, 0, 0, 8));
        modelButton = button("Import model");
        modelButton.setOnClickListener(view -> chooseFile(REQUEST_MODEL));
        content.addView(modelButton, margin(matchWrap(), 0, 0, 0, 20));

        content.addView(text("Offline Wikipedia dump (.zim)", 18, true), matchWrap());
        wikipediaStatus = text("", 14, false);
        content.addView(wikipediaStatus, margin(matchWrap(), 4, 0, 0, 8));
        wikipediaButton = button("Import ZIM file");
        wikipediaButton.setOnClickListener(view -> chooseFile(REQUEST_WIKIPEDIA));
        content.addView(wikipediaButton, margin(matchWrap(), 0, 0, 0, 24));

        content.addView(text("Ask a question", 18, true), matchWrap());
        EditText question = new EditText(this);
        question.setHint("Describe what you want to know");
        question.setMinLines(2);
        question.setGravity(Gravity.TOP | Gravity.START);
        content.addView(question, margin(matchWrap(), 6, 0, 0, 8));

        Button askButton = button("Ask offline assistant");
        askButton.setOnClickListener(view -> {
            answer.setText("I don't know. This version cannot yet read ZIM articles "
                    + "or run the local model, so it won't guess.");
        });
        content.addView(askButton, matchWrap());
        answer = text("Answers will only be shown when they can be supported by local articles.",
                16, false);
        content.addView(answer, margin(matchWrap(), 16, 0, 0, 0));

        ScrollView scrollView = new ScrollView(this);
        scrollView.addView(content);
        setContentView(scrollView);
    }

    private void chooseFile(int requestCode) {
        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        intent.addCategory(Intent.CATEGORY_OPENABLE);
        intent.setType("*/*");
        startActivityForResult(intent, requestCode);
    }

    @Override
    @SuppressWarnings("deprecation")
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (resultCode != RESULT_OK || data == null || data.getData() == null) {
            return;
        }
        if (requestCode != REQUEST_MODEL && requestCode != REQUEST_WIKIPEDIA) {
            return;
        }

        Uri uri = data.getData();
        String expectedExtension = requestCode == REQUEST_MODEL ? ".gguf" : ".zim";
        String destinationName = requestCode == REQUEST_MODEL ? MODEL_FILE : WIKIPEDIA_FILE;
        String displayName = getDisplayName(uri);
        if (displayName == null || !displayName.toLowerCase().endsWith(expectedExtension)) {
            Toast.makeText(this, "Select a " + expectedExtension + " file.", Toast.LENGTH_LONG).show();
            return;
        }

        setImporting(requestCode, true);
        importExecutor.execute(() -> {
            String result;
            try {
                copyIntoPrivateStorage(uri, destinationName);
                result = "Imported " + displayName + " into app storage.";
            } catch (Exception exception) {
                result = "Import failed: " + exception.getMessage();
            }
            String finalResult = result;
            runOnUiThread(() -> {
                setImporting(requestCode, false);
                refreshResourceStatus();
                Toast.makeText(this, finalResult, Toast.LENGTH_LONG).show();
            });
        });
    }

    private void copyIntoPrivateStorage(Uri source, String destinationName) throws Exception {
        File directory = getFilesDir();
        File destination = new File(directory, destinationName);
        File temporary = new File(directory, destinationName + ".partial");
        try (InputStream input = getContentResolver().openInputStream(source)) {
            if (input == null) {
                throw new IllegalStateException("The selected file could not be opened.");
            }
            try (FileOutputStream output = new FileOutputStream(temporary)) {
                byte[] buffer = new byte[64 * 1024];
                int count;
                while ((count = input.read(buffer)) != -1) {
                    output.write(buffer, 0, count);
                }
                output.getFD().sync();
            }
            Files.move(temporary.toPath(), destination.toPath(),
                    StandardCopyOption.REPLACE_EXISTING);
        } catch (Exception exception) {
            temporary.delete();
            throw exception;
        }
    }

    private String getDisplayName(Uri uri) {
        try (Cursor cursor = getContentResolver().query(uri,
                new String[]{OpenableColumns.DISPLAY_NAME}, null, null, null)) {
            if (cursor != null && cursor.moveToFirst()) {
                return cursor.getString(0);
            }
        }
        return null;
    }

    private void refreshResourceStatus() {
        File model = new File(getFilesDir(), MODEL_FILE);
        File wikipedia = new File(getFilesDir(), WIKIPEDIA_FILE);
        modelStatus.setText(model.exists()
                ? "Stored privately · " + formatSize(model.length())
                : "No model imported");
        wikipediaStatus.setText(wikipedia.exists()
                ? "Stored privately · " + formatSize(wikipedia.length())
                : "No Wikipedia dump imported");
    }

    private void setImporting(int requestCode, boolean importing) {
        if (requestCode == REQUEST_MODEL) {
            modelButton.setEnabled(!importing);
            modelButton.setText(importing ? "Importing…" : "Import model");
            modelStatus.setText(importing ? "Copying into private app storage…" : modelStatus.getText());
        } else {
            wikipediaButton.setEnabled(!importing);
            wikipediaButton.setText(importing ? "Importing…" : "Import ZIM file");
            wikipediaStatus.setText(importing ? "Copying into private app storage…" : wikipediaStatus.getText());
        }
    }

    private String formatSize(long bytes) {
        if (bytes >= 1_073_741_824L) {
            return String.format("%.1f GB", bytes / 1_073_741_824.0);
        }
        if (bytes >= 1_048_576L) {
            return String.format("%.1f MB", bytes / 1_048_576.0);
        }
        return String.format("%.1f KB", bytes / 1_024.0);
    }

    private TextView text(String value, int size, boolean bold) {
        TextView view = new TextView(this);
        view.setText(value);
        view.setTextSize(size);
        if (bold) {
            view.setTypeface(null, android.graphics.Typeface.BOLD);
        }
        return view;
    }

    private Button button(String label) {
        Button button = new Button(this);
        button.setText(label);
        return button;
    }

    private int dp(int value) {
        return (int) (value * getResources().getDisplayMetrics().density + 0.5f);
    }

    private LinearLayout.LayoutParams matchWrap() {
        return new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT);
    }

    private LinearLayout.LayoutParams margin(
            LinearLayout.LayoutParams params, int top, int left, int right, int bottom) {
        params.setMargins(dp(left), dp(top), dp(right), dp(bottom));
        return params;
    }

    @Override
    protected void onDestroy() {
        importExecutor.shutdown();
        super.onDestroy();
    }
}
