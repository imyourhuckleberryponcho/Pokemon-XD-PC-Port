package net.pokemonxdpc.port;

import java.io.FileOutputStream;
import java.io.InputStream;
import java.net.HttpURLConnection;
import java.net.URL;

/**
 * The updater's download on Android (engine/update/update_fetch_posix.cpp, docs/ANDROID.md "The updater"): Android has no curl, so the
 * system's own HTTPS client does it (certificates, proxy and TLS of the phone). Whatever arrives is never trusted here: the signed update.json
 * and the SHA-256 in it are checked by the C++ side before anything is used. Called from native code (platform/os/android_install.cpp).
 */
public class HttpFetch {
    /** Set from native code to stop a download that is running (the player pressed Cancel). */
    public static volatile boolean cancel = false;

    /**
     * Downloads the address into the file. Returns "" when done, else a short reason. Redirects are followed (at most 5) only to https (or, when
     * https-only is off, http); a download larger than maxBytes stops at once; a stalled connection ends after timeoutMs.
     */
    public static String fetch(String url, String file, long maxBytes, int timeoutMs, boolean httpsOnly, String userAgent) {
        cancel = false;
        try {
            String current = url;
            HttpURLConnection connection = null;
            for (int hop = 0; ; hop++) {
                if (hop > 5) {
                    return "too many redirects";
                }
                if (httpsOnly && !current.startsWith("https://")) {
                    return "only https:// addresses are used here";
                }
                if (!current.startsWith("https://") && !current.startsWith("http://")) {
                    return "the address isn't one this program downloads from";
                }
                if (hop > 0 && url.startsWith("https://") && !current.startsWith("https://")) {
                    return "a redirect to a plain http address was refused";
                }
                connection = (HttpURLConnection) new URL(current).openConnection();
                connection.setInstanceFollowRedirects(false);
                connection.setConnectTimeout(timeoutMs);
                connection.setReadTimeout(timeoutMs);
                connection.setRequestProperty("User-Agent", userAgent);
                connection.setRequestProperty("Accept-Encoding", "identity");
                int code = connection.getResponseCode();
                if (code == 301 || code == 302 || code == 303 || code == 307 || code == 308) {
                    String next = connection.getHeaderField("Location");
                    connection.disconnect();
                    if (next == null) {
                        return "a redirect without an address";
                    }
                    current = new URL(new URL(current), next).toString();
                    continue;
                }
                if (code < 200 || code > 299) {
                    String message = "HTTP error " + code;
                    connection.disconnect();
                    return message;
                }
                break;
            }
            long length = connection.getContentLengthLong();
            if (length > maxBytes) {
                connection.disconnect();
                return "the download is larger than expected";
            }
            long done = 0;
            try (InputStream in = connection.getInputStream(); FileOutputStream out = new FileOutputStream(file)) {
                byte[] buffer = new byte[1 << 16];
                int n;
                while ((n = in.read(buffer)) > 0) {
                    done += n;
                    if (done > maxBytes) {
                        return "the download is larger than expected";
                    }
                    if (cancel) {
                        return "stopped";
                    }
                    out.write(buffer, 0, n);
                }
            } finally {
                connection.disconnect();
            }
            return "";
        } catch (Exception e) {
            return String.valueOf(e.getMessage() != null ? e.getMessage() : e);
        }
    }
}
