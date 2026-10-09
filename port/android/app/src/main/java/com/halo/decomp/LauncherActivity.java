package com.halo.decomp;

import android.app.Activity;
import android.content.Intent;
import android.graphics.Color;
import android.net.Uri;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.os.ParcelFileDescriptor;
import android.provider.Settings;
import android.util.TypedValue;
import android.view.Gravity;
import android.view.View;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ProgressBar;
import android.widget.TextView;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import java.util.ArrayList;
import java.util.List;
import java.nio.channels.FileChannel;

/**
 * Starts the game once its data is in place.
 *
 * The game reads the Xbox game data (the folder holding maps/) from the
 * app's external files directory,
 * /sdcard/Android/data/dev.horrible.chupathingyce/files.
 * If it is missing, this screen lets the player pick an Xbox disc image of
 * the game (.xiso or .iso, any version) with the system file picker, and
 * copies its maps folder there (XisoExtractor), as the desktop games do; or
 * download it (MAPS_URL: a folder holding the files and maps.txt, their names
 * and sizes, as the desktop games' data.download_url); or they can push the
 * maps folder with adb.
 */
public class LauncherActivity extends Activity {
    private static final int PICK_IMAGE = 1;
    /* where the maps folder can be downloaded from (services/selfhost's
       assets/maps/, with maps.txt) */
    private static final String MAPS_URL = "https://halo.fractumseraph.net/assets/maps/";

    private File dataRoot;
    private TextView status;
    private ProgressBar progress;
    private Button pick;
    private Button download;
    private final Handler handler = new Handler(Looper.getMainLooper());

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        dataRoot = getExternalFilesDir(null);
        // created by the app, so that files pushed into it with adb stay
        // readable (a directory adb creates there belongs to the shell user)
        if (dataRoot != null)
            new File(dataRoot, "maps").mkdirs();
        passOnHardwareId();
        passOnInvite(getIntent());
        if (haveData()) {
            startGame();
            return;
        }
        buildInterface();
    }

    /**
     * An internet play invite link the app was opened with: the game
     * (port/linux/src/p2p.c) picks it up from join_link.txt, whether it is
     * starting now or already running.
     */
    private void passOnInvite(Intent intent) {
        if (intent == null || !Intent.ACTION_VIEW.equals(intent.getAction()) || intent.getData() == null
            || dataRoot == null)
            return;
        // written whole under another name, then renamed: the game never
        // reads it half written
        File partial = new File(dataRoot, "join_link.txt.tmp");
        try (OutputStream out = new FileOutputStream(partial)) {
            out.write(intent.getData().toString().getBytes("UTF-8"));
        } catch (java.io.IOException e) {
            // the link is lost; the player can copy it instead
            partial.delete();
            return;
        }
        if (!partial.renameTo(new File(dataRoot, "join_link.txt")))
            partial.delete();
    }

    /**
     * This device's ANDROID_ID (the app's own: one per app signing key and
     * user, until a factory reset), which native code cannot read: the game
     * (port/linux/src/p2p.c) hashes it from hardware_id.txt into the
     * hardware id a host it joins is told.
     */
    private void passOnHardwareId() {
        String id;

        if (dataRoot == null)
            return;
        try {
            id = Settings.Secure.getString(getContentResolver(), Settings.Secure.ANDROID_ID);
        } catch (RuntimeException e) {
            return;
        }
        if (id == null || id.isEmpty())
            return;
        File partial = new File(dataRoot, "hardware_id.txt.tmp");
        try (OutputStream out = new FileOutputStream(partial)) {
            out.write(id.getBytes("UTF-8"));
        } catch (java.io.IOException e) {
            partial.delete();
            return;
        }
        if (!partial.renameTo(new File(dataRoot, "hardware_id.txt")))
            partial.delete();
    }

    private boolean haveData() {
        return dataRoot != null && new File(dataRoot, "maps/ui.map").isFile();
    }

    private void startGame() {
        startActivity(new Intent(this, HaloActivity.class));
        finish();
    }

    private int dp(float value) {
        return (int) TypedValue.applyDimension(TypedValue.COMPLEX_UNIT_DIP, value,
            getResources().getDisplayMetrics());
    }

    private void buildInterface() {
        LinearLayout layout = new LinearLayout(this);
        layout.setOrientation(LinearLayout.VERTICAL);
        layout.setGravity(Gravity.CENTER);
        layout.setPadding(dp(48), dp(24), dp(48), dp(24));
        layout.setBackgroundColor(Color.rgb(12, 16, 20));

        TextView title = new TextView(this);
        title.setText("Halo needs its game data");
        title.setTextColor(Color.WHITE);
        title.setTextSize(TypedValue.COMPLEX_UNIT_SP, 24);
        title.setGravity(Gravity.CENTER);
        layout.addView(title);

        TextView message = new TextView(this);
        message.setText("Download the game's maps folder (about 1.8 GB) into the app's storage, or choose "
            + "an Xbox disc image of Halo: Combat Evolved (an .iso or .xiso file, any version) on this "
            + "device to copy it from; you can delete the image afterwards.\n\n"
            + "You can also copy a maps folder from a computer:\n"
            + "adb push <folder with maps>/. " + (dataRoot != null ? dataRoot.getAbsolutePath() : "") + "/");
        message.setTextColor(Color.rgb(200, 205, 210));
        message.setTextSize(TypedValue.COMPLEX_UNIT_SP, 15);
        message.setGravity(Gravity.CENTER);
        message.setPadding(0, dp(16), 0, dp(16));
        layout.addView(message);

        download = new Button(this);
        download.setText("Download the maps (1.8 GB)");
        download.setOnClickListener(v -> {
            setBusy(true);
            status.setText("Asking for the list of maps...");
            new Thread(this::downloadMaps).start();
        });
        layout.addView(download, new LinearLayout.LayoutParams(LinearLayout.LayoutParams.WRAP_CONTENT,
            LinearLayout.LayoutParams.WRAP_CONTENT));

        pick = new Button(this);
        pick.setText("Choose disc image");
        pick.setOnClickListener(v -> {
            // (disc images have no MIME type of their own: any file, checked
            // when it is read)
            Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
            intent.addCategory(Intent.CATEGORY_OPENABLE);
            intent.setType("*/*");
            startActivityForResult(intent, PICK_IMAGE);
        });
        layout.addView(pick, new LinearLayout.LayoutParams(LinearLayout.LayoutParams.WRAP_CONTENT,
            LinearLayout.LayoutParams.WRAP_CONTENT));

        progress = new ProgressBar(this, null, android.R.attr.progressBarStyleHorizontal);
        progress.setMax(1000);
        progress.setVisibility(View.GONE);
        LinearLayout.LayoutParams progressLayout = new LinearLayout.LayoutParams(dp(480),
            LinearLayout.LayoutParams.WRAP_CONTENT);
        progressLayout.topMargin = dp(16);
        layout.addView(progress, progressLayout);

        status = new TextView(this);
        status.setTextColor(Color.rgb(160, 200, 160));
        status.setGravity(Gravity.CENTER);
        status.setPadding(0, dp(8), 0, 0);
        layout.addView(status);

        setContentView(layout);
        download.requestFocus();
    }

    @Override
    protected void onResume() {
        super.onResume();
        // data pushed with adb while this screen was open
        if (pick != null && pick.isEnabled() && haveData())
            startGame();
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode != PICK_IMAGE || resultCode != RESULT_OK || data == null || data.getData() == null)
            return;
        Uri image = data.getData();
        setBusy(true);
        status.setText("Reading the disc image...");
        new Thread(() -> importImage(image)).start();
    }

    private void report(String text, int permille) {
        handler.post(() -> {
            status.setText(text);
            if (permille >= 0)
                progress.setProgress(permille);
        });
    }

    private void setBusy(boolean busy) {
        pick.setEnabled(!busy);
        download.setEnabled(!busy);
        progress.setVisibility(busy ? View.VISIBLE : View.GONE);
        if (!busy)
            download.requestFocus();
    }

    private void fail(String text) {
        handler.post(() -> {
            status.setText(text);
            setBusy(false);
        });
    }

    private static HttpURLConnection open(String url) throws java.io.IOException {
        HttpURLConnection connection = (HttpURLConnection) new URL(url).openConnection();
        connection.setConnectTimeout(20000);
        connection.setReadTimeout(60000);
        connection.setRequestProperty("User-Agent", "OpenCE");
        if (connection.getResponseCode() != HttpURLConnection.HTTP_OK)
            throw new java.io.IOException(url + " answered " + connection.getResponseCode());
        return connection;
    }

    /**
     * Downloads the maps folder from MAPS_URL: maps.txt, then each file into
     * <name>.partial, renamed once its size is right. Files already here at
     * their size are kept, so a download stopped part way goes on.
     */
    private void downloadMaps() {
        try {
            File folder = new File(dataRoot, "maps");
            folder.mkdirs();
            List<String> names = new ArrayList<>();
            List<Long> sizes = new ArrayList<>();
            long total = 0;
            HttpURLConnection list = open(MAPS_URL + "maps.txt");
            try (java.io.BufferedReader reader = new java.io.BufferedReader(
                    new java.io.InputStreamReader(list.getInputStream(), "UTF-8"))) {
                String line;
                while ((line = reader.readLine()) != null) {
                    String[] parts = line.split("\t");
                    if (parts.length < 2 || !parts[0].matches("[A-Za-z0-9_-][A-Za-z0-9_.-]{0,62}"))
                        continue;
                    long size = Long.parseLong(parts[1].trim());
                    names.add(parts[0]);
                    sizes.add(size);
                    total += size;
                }
            } finally {
                list.disconnect();
            }
            if (names.isEmpty())
                throw new java.io.IOException("the list of maps is empty");
            long done = 0;
            byte[] buffer = new byte[1 << 16];
            for (int which = 0; which < names.size(); which++) {
                String name = names.get(which);
                long size = sizes.get(which);
                File file = new File(folder, name);
                if (file.isFile() && file.length() == size) {
                    done += size;
                    continue;
                }
                File partial = new File(folder, name + ".partial");
                HttpURLConnection connection = open(MAPS_URL + name);
                try (InputStream in = connection.getInputStream();
                     OutputStream out = new FileOutputStream(partial)) {
                    long received = 0;
                    long reported = 0;
                    int read;
                    while ((read = in.read(buffer)) > 0) {
                        out.write(buffer, 0, read);
                        received += read;
                        if (received - reported >= (1 << 20)) {
                            reported = received;
                            long at = done + received;
                            report("Downloading maps/" + name + " (" + (at >> 20) + " of " + (total >> 20) + " MB)",
                                (int) (at * 1000 / total));
                        }
                    }
                } finally {
                    connection.disconnect();
                }
                if (partial.length() != size) {
                    partial.delete();
                    throw new java.io.IOException(name + " did not arrive whole");
                }
                file.delete();
                if (!partial.renameTo(file))
                    throw new java.io.IOException("could not put " + name + " in place");
                done += size;
            }
            handler.post(() -> {
                if (haveData()) {
                    startGame();
                } else {
                    fail("The download finished but maps/ui.map is missing.");
                }
            });
        } catch (Exception exception) {
            fail("Downloading failed: " + exception.getMessage() + ". Try again.");
        }
    }

    private void importImage(Uri image) {
        try (ParcelFileDescriptor descriptor = getContentResolver().openFileDescriptor(image, "r")) {
            if (descriptor == null)
                throw new java.io.IOException("the file could not be opened");
            try (FileInputStream in = new FileInputStream(descriptor.getFileDescriptor())) {
                FileChannel channel = in.getChannel();

                XisoExtractor.extractMaps(channel, dataRoot, (file, done, total) ->
                    report("Extracting maps/" + file + " (" + (done >> 20) + " of " + (total >> 20) + " MB)",
                        total > 0 ? (int) (done * 1000 / total) : 0));
            }
            handler.post(() -> {
                if (haveData()) {
                    startGame();
                } else {
                    fail("The extraction finished but maps/ui.map is missing.");
                }
            });
        } catch (XisoExtractor.ExtractException exception) {
            fail(exception.getMessage());
        } catch (Exception exception) {
            fail("Extracting failed: " + exception.getMessage());
        }
    }
}
