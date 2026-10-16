package com.halo.decomp;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;

/**
 * Downloads the game's maps folder from MAPS_URL, as the desktop games do
 * (port/linux/src/sdl_platform.c, data.download_url): maps.txt, a line each
 * (a file's name, a tab, its size), then each file into <name>.partial,
 * renamed once its size is right. Files already here at their size are kept,
 * so a download stopped part way goes on.
 *
 * Which files: all of them; all but the campaign's maps (MULTIPLAYER, the
 * first start's choice: about a quarter of the size); or the campaign's maps
 * alone (CAMPAIGN, later: the touch controls' Options, the launcher icon's
 * shortcut). The list also names Halo Custom Edition's shared maps as
 * custom_maps/<name> (bitmaps.map, sounds.map, loc.map, which every Custom
 * Edition map needs), from the site's custom_maps folder beside maps/, into
 * maps_ce; a copy the player already has there is kept, whatever its size.
 */
final class MapsDownloader {
    /* where the maps folder can be downloaded from (services/selfhost's
       assets/maps/, with maps.txt) */
    static final String MAPS_URL = "https://halo.fractumseraph.net/assets/maps/";
    static final String CUSTOM_MAPS_URL = "https://halo.fractumseraph.net/assets/custom_maps/";

    static final int ALL = 0;
    static final int MULTIPLAYER = 1;
    static final int CAMPAIGN = 2;

    private static final String[] CAMPAIGN_LEVELS = {
        "a10", "a30", "a50", "b30", "b40", "c10", "c20", "c40", "d20", "d40"
    };
    private static final String[] CUSTOM_EDITION_SHARED = { "bitmaps.map", "sounds.map", "loc.map" };

    interface Progress {
        /** what is happening, and how far (0 to 1000; -1 for not known) */
        void report(String text, int permille);
    }

    private MapsDownloader() {
    }

    /** whether a maps folder's file is one of the campaign's ten levels */
    static boolean isCampaignMap(String name) {
        String lower = name.toLowerCase(Locale.ROOT);
        for (String level : CAMPAIGN_LEVELS) {
            if (lower.equals(level + ".map"))
                return true;
        }
        return false;
    }

    /** whether every campaign map is in the maps folder */
    static boolean haveCampaign(File dataRoot) {
        for (String level : CAMPAIGN_LEVELS) {
            if (!new File(dataRoot, "maps/" + level + ".map").isFile())
                return false;
        }
        return true;
    }

    private static boolean isCustomEditionShared(String name) {
        for (String shared : CUSTOM_EDITION_SHARED) {
            if (shared.equalsIgnoreCase(name))
                return true;
        }
        return false;
    }

    private static HttpURLConnection open(String url) throws IOException {
        HttpURLConnection connection = (HttpURLConnection) new URL(url).openConnection();
        connection.setConnectTimeout(20000);
        connection.setReadTimeout(60000);
        connection.setRequestProperty("User-Agent", "OpenCE");
        if (connection.getResponseCode() != HttpURLConnection.HTTP_OK)
            throw new IOException(url + " answered " + connection.getResponseCode());
        return connection;
    }

    /** downloads the files wanted (ALL, MULTIPLAYER or CAMPAIGN) into dataRoot's maps and maps_ce */
    static void download(File dataRoot, int wanted, Progress progress) throws IOException {
        File folder = new File(dataRoot, "maps");
        File customEditionFolder = new File(dataRoot, "maps_ce");
        folder.mkdirs();
        List<String> names = new ArrayList<>();
        List<Long> sizes = new ArrayList<>();
        List<Boolean> customEdition = new ArrayList<>();
        long total = 0;
        HttpURLConnection list = open(MAPS_URL + "maps.txt");
        try (java.io.BufferedReader reader = new java.io.BufferedReader(
                new java.io.InputStreamReader(list.getInputStream(), "UTF-8"))) {
            String line;
            while ((line = reader.readLine()) != null) {
                String[] parts = line.split("\t");
                if (parts.length < 2)
                    continue;
                String name = parts[0];
                boolean shared = false;
                if (name.startsWith("custom_maps/")) {
                    name = name.substring("custom_maps/".length());
                    shared = true;
                    if (!isCustomEditionShared(name))
                        continue;
                }
                if (!name.matches("[A-Za-z0-9_-][A-Za-z0-9_.-]{0,62}"))
                    continue;
                // (the files wanted: the campaign's maps alone, or all but them)
                if (wanted == CAMPAIGN ? shared || !isCampaignMap(name)
                        : wanted == MULTIPLAYER && !shared && isCampaignMap(name))
                    continue;
                long size = Long.parseLong(parts[1].trim());
                names.add(name);
                sizes.add(size);
                customEdition.add(shared);
                total += size;
            }
        } finally {
            list.disconnect();
        }
        if (names.isEmpty())
            throw new IOException("the list of maps is empty");
        long done = 0;
        byte[] buffer = new byte[1 << 16];
        for (int which = 0; which < names.size(); which++) {
            String name = names.get(which);
            long size = sizes.get(which);
            boolean shared = customEdition.get(which);
            File target = shared ? customEditionFolder : folder;
            File file = new File(target, name);
            // (a player's own copy of a Custom Edition shared map is kept)
            if (shared ? file.exists() : file.isFile() && file.length() == size) {
                done += size;
                continue;
            }
            target.mkdirs();
            File partial = new File(target, name + ".partial");
            HttpURLConnection connection = open((shared ? CUSTOM_MAPS_URL : MAPS_URL) + name);
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
                        progress.report("Downloading " + (shared ? "maps_ce/" : "maps/") + name + " ("
                            + (at >> 20) + " of " + (total >> 20) + " MB)", (int) (at * 1000 / total));
                    }
                }
            } finally {
                connection.disconnect();
            }
            if (partial.length() != size) {
                partial.delete();
                throw new IOException(name + " did not arrive whole");
            }
            file.delete();
            if (!partial.renameTo(file))
                throw new IOException("could not put " + name + " in place");
            done += size;
        }
    }
}
