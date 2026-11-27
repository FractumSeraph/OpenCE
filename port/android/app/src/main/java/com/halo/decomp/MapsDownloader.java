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
 * And Halo MCC's shared sets as mcc_shared/singleplayer/bitmaps.map and
 * mcc_shared/multiplayer/bitmaps.map and sounds.map, which Workshop maps
 * that leave textures and sounds to MCC's need, into mcc_maps/_shared/<set>
 * (maps_mcc where the game has that folder): ALL takes them, and MCC_SHARED
 * takes them alone (the touch controls' Options); a copy already there is
 * kept too (the desktop games: sdl_platform.c's DATA_MAPS_MCC_SHARED).
 * And Halo PC's own bitmaps.map and sounds.map as pc_shared/<name>, into
 * maps_pc, where Halo PC retail maps and HaloMD's read their stock textures
 * and sounds. SHARED_FILES takes every shared file and no map: the game
 * downloads those it lacks at each start, in the background, without asking
 * (HaloActivity.downloadSharedFiles; the desktop's data.shared_files).
 * One download at a time: they may write the same files.
 */
final class MapsDownloader {
    /* where the maps folder can be downloaded from (services/selfhost's
       assets/maps/, with maps.txt) */
    static final String MAPS_URL = "https://halo.fractumseraph.net/assets/maps/";
    static final String CUSTOM_MAPS_URL = "https://halo.fractumseraph.net/assets/custom_maps/";
    static final String MCC_SHARED_URL = "https://halo.fractumseraph.net/assets/mcc_shared/";
    static final String PC_SHARED_URL = "https://halo.fractumseraph.net/assets/pc_shared/";

    static final int ALL = 0;
    static final int MULTIPLAYER = 1;
    static final int CAMPAIGN = 2;
    static final int MCC_SHARED = 3;
    static final int SHARED_FILES = 4;

    /* the folder a file of the list goes into */
    private static final int FOLDER_MAPS = 0;
    private static final int FOLDER_CUSTOM_EDITION = 1;
    private static final int FOLDER_MCC_SINGLEPLAYER = 2;
    private static final int FOLDER_MCC_MULTIPLAYER = 3;
    private static final int FOLDER_HALO_PC = 4;

    private static final String[] CAMPAIGN_LEVELS = {
        "a10", "a30", "a50", "b30", "b40", "c10", "c20", "c40", "d20", "d40"
    };
    private static final String[] CUSTOM_EDITION_SHARED = { "bitmaps.map", "sounds.map", "loc.map" };
    private static final String[] HALO_PC_SHARED = { "bitmaps.map", "sounds.map" };
    /* MCC's shared sets' files, as the game looks for them (mcc_package.c) */
    private static final String[] MCC_SHARED_FILES = {
        "singleplayer/bitmaps.map", "multiplayer/bitmaps.map", "multiplayer/sounds.map"
    };

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

    /** the game's MCC maps folder: maps_mcc, or mcc_maps where only that one is (mcc_maps.c) */
    static File mccFolder(File dataRoot) {
        File folder = new File(dataRoot, "maps_mcc");
        File fallback = new File(dataRoot, "mcc_maps");
        return !folder.isDirectory() && fallback.isDirectory() ? fallback : folder;
    }

    /** whether MCC's three shared files are all here */
    static boolean haveMccShared(File dataRoot) {
        File shared = new File(mccFolder(dataRoot), "_shared");
        for (String file : MCC_SHARED_FILES) {
            if (!new File(shared, file).isFile())
                return false;
        }
        return true;
    }

    /** whether a download (ALL, MULTIPLAYER, CAMPAIGN, MCC_SHARED or SHARED_FILES) takes a file of the list */
    private static boolean wanted(int wanted, String name, int folder) {
        if (wanted == CAMPAIGN)
            return folder == FOLDER_MAPS && isCampaignMap(name);
        if (wanted == MULTIPLAYER)
            return folder == FOLDER_CUSTOM_EDITION || (folder == FOLDER_MAPS && !isCampaignMap(name));
        if (wanted == MCC_SHARED)
            return folder == FOLDER_MCC_SINGLEPLAYER || folder == FOLDER_MCC_MULTIPLAYER;
        if (wanted == SHARED_FILES)
            return folder != FOLDER_MAPS;
        return true;
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

    /**
     * downloads the files wanted (ALL, MULTIPLAYER, CAMPAIGN, MCC_SHARED or SHARED_FILES) into dataRoot's
     * maps, maps_ce, the MCC maps folder's _shared and maps_pc
     */
    static synchronized void download(File dataRoot, int wanted, Progress progress) throws IOException {
        File folder = new File(dataRoot, "maps");
        File customEditionFolder = new File(dataRoot, "maps_ce");
        File mccShared = new File(mccFolder(dataRoot), "_shared");
        File haloPcFolder = new File(dataRoot, "maps_pc");
        folder.mkdirs();
        List<String> names = new ArrayList<>();
        List<Long> sizes = new ArrayList<>();
        List<Integer> folders = new ArrayList<>();
        List<String> hashes = new ArrayList<>();
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
                int kind = FOLDER_MAPS;
                if (name.startsWith("custom_maps/")) {
                    name = name.substring("custom_maps/".length());
                    kind = FOLDER_CUSTOM_EDITION;
                    if (!isCustomEditionShared(name))
                        continue;
                } else if (name.startsWith("mcc_shared/")) {
                    String file = name.substring("mcc_shared/".length());
                    boolean known = false;
                    for (String shared : MCC_SHARED_FILES)
                        known |= shared.equalsIgnoreCase(file);
                    if (!known)
                        continue;
                    kind = file.toLowerCase(Locale.ROOT).startsWith("singleplayer/")
                        ? FOLDER_MCC_SINGLEPLAYER : FOLDER_MCC_MULTIPLAYER;
                    name = file.substring(file.indexOf('/') + 1);
                } else if (name.startsWith("pc_shared/")) {
                    name = name.substring("pc_shared/".length());
                    kind = FOLDER_HALO_PC;
                    boolean known = false;
                    for (String shared : HALO_PC_SHARED)
                        known |= shared.equalsIgnoreCase(name);
                    if (!known)
                        continue;
                }
                if (!name.matches("[A-Za-z0-9_-][A-Za-z0-9_.-]{0,62}"))
                    continue;
                if (!wanted(wanted, name, kind))
                    continue;
                long size = Long.parseLong(parts[1].trim());
                names.add(name);
                sizes.add(size);
                folders.add(kind);
                // (a third column: the file's SHA-256, the MCC sets' files')
                hashes.add(parts.length > 2 && parts[2].trim().matches("[0-9a-fA-F]{64}")
                    ? parts[2].trim().toLowerCase(Locale.ROOT) : null);
                total += size;
            }
        } finally {
            list.disconnect();
        }
        if (names.isEmpty())
            throw new IOException(wanted == MCC_SHARED ? "the site lists no MCC shared files"
                : wanted == SHARED_FILES ? "the site lists no shared files" : "the list of maps is empty");
        long done = 0;
        byte[] buffer = new byte[1 << 16];
        for (int which = 0; which < names.size(); which++) {
            String name = names.get(which);
            long size = sizes.get(which);
            int kind = folders.get(which);
            String hash = hashes.get(which);
            boolean shared = kind != FOLDER_MAPS;
            String set = kind == FOLDER_MCC_SINGLEPLAYER ? "singleplayer" : "multiplayer";
            File target = kind == FOLDER_MAPS ? folder : kind == FOLDER_CUSTOM_EDITION ? customEditionFolder
                : kind == FOLDER_HALO_PC ? haloPcFolder : new File(mccShared, set);
            File file = new File(target, name);
            // (a player's own copy of a shared map, Custom Edition's, MCC's or Halo PC's, is kept)
            if (shared ? file.exists() : file.isFile() && file.length() == size) {
                done += size;
                continue;
            }
            target.mkdirs();
            File partial = new File(target, name + ".partial");
            // (MCC's multiplayer set is Halo Custom Edition's own bitmaps.map and
            // sounds.map: maps_ce's copy, when its SHA-256 says it is the same
            // file, is copied instead of downloaded again)
            File customEditionCopy = new File(customEditionFolder, name);
            if (kind == FOLDER_MCC_MULTIPLAYER && hash != null && customEditionCopy.length() == size
                    && hash.equals(sha256(customEditionCopy))) {
                progress.report("Copying maps_ce/" + name, (int) (done * 1000 / total));
                copy(customEditionCopy, partial, buffer);
                if (partial.length() == size && partial.renameTo(file)) {
                    done += size;
                    continue;
                }
                partial.delete();
            }
            String url = kind == FOLDER_MAPS ? MAPS_URL + name : kind == FOLDER_CUSTOM_EDITION
                ? CUSTOM_MAPS_URL + name : kind == FOLDER_HALO_PC ? PC_SHARED_URL + name
                : MCC_SHARED_URL + set + "/" + name;
            String shown = kind == FOLDER_MAPS ? "maps/" : kind == FOLDER_CUSTOM_EDITION ? "maps_ce/"
                : kind == FOLDER_HALO_PC ? "maps_pc/" : mccShared.getParentFile().getName() + "/_shared/" + set + "/";
            HttpURLConnection connection = open(url);
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
                        progress.report("Downloading " + shown + name + " ("
                            + (at >> 20) + " of " + (total >> 20) + " MB)", (int) (at * 1000 / total));
                    }
                }
            } finally {
                connection.disconnect();
            }
            if (partial.length() != size || (hash != null && !hash.equals(sha256(partial)))) {
                partial.delete();
                throw new IOException(name + " did not arrive whole");
            }
            file.delete();
            if (!partial.renameTo(file))
                throw new IOException("could not put " + name + " in place");
            done += size;
        }
    }

    /** a file's SHA-256, in lowercase hex */
    private static String sha256(File file) throws IOException {
        java.security.MessageDigest digest;
        try {
            digest = java.security.MessageDigest.getInstance("SHA-256");
        } catch (java.security.NoSuchAlgorithmException e) {
            throw new IOException("no SHA-256 here");
        }
        byte[] buffer = new byte[1 << 16];
        try (InputStream in = new java.io.FileInputStream(file)) {
            int read;
            while ((read = in.read(buffer)) > 0)
                digest.update(buffer, 0, read);
        }
        StringBuilder text = new StringBuilder();
        for (byte value : digest.digest())
            text.append(String.format(Locale.ROOT, "%02x", value & 0xff));
        return text.toString();
    }

    private static void copy(File from, File to, byte[] buffer) throws IOException {
        try (InputStream in = new java.io.FileInputStream(from);
             OutputStream out = new FileOutputStream(to)) {
            int read;
            while ((read = in.read(buffer)) > 0)
                out.write(buffer, 0, read);
        }
    }
}
