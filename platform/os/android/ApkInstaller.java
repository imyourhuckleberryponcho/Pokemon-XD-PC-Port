package net.pokemonxdpc.port;

import android.app.Activity;
import android.app.PendingIntent;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.pm.PackageInstaller;
import android.database.Cursor;
import android.net.Uri;
import android.provider.OpenableColumns;
import android.util.Log;

import java.io.FileInputStream;
import java.io.InputStream;
import java.io.OutputStream;

/**
 * Hands an APK file the app has downloaded and checked to Android's installer (docs/ANDROID.md "Packaging", the updater).
 * The PackageInstaller session needs no FileProvider and no extra library: the bytes are written into the session, Android asks
 * the player to confirm (and, once, to allow this app to install apps), and replaces the app. The player's data stays (same
 * package name, same signing key). Android refuses a version that is not newer (versionCode) or a different signing key.
 * Called from native code (platform/os/android_install.cpp).
 */
public class ApkInstaller extends BroadcastReceiver {
    private static final String TAG = "XDinstall";
    private static final String ACTION = "net.pokemonxdpc.port.INSTALL_RESULT";

    /** The file name Android's document provider shows for a content:// address ("" when it does not say). */
    public static String displayName(Activity activity, String uri) {
        try (Cursor cursor = activity.getContentResolver().query(Uri.parse(uri), new String[] {OpenableColumns.DISPLAY_NAME}, null, null, null)) {
            if (cursor != null && cursor.moveToFirst()) {
                String name = cursor.getString(0);
                return name != null ? name : "";
            }
        } catch (Exception e) {
            Log.w(TAG, "no display name for " + uri, e);
        }
        return "";
    }

    /** Returns "" when the session was handed to Android, else why not. */
    public static String install(Activity activity, String apkPath) {
        try {
            PackageInstaller installer = activity.getPackageManager().getPackageInstaller();
            PackageInstaller.SessionParams params = new PackageInstaller.SessionParams(PackageInstaller.SessionParams.MODE_FULL_INSTALL);
            int id = installer.createSession(params);
            PackageInstaller.Session session = installer.openSession(id);
            try {
                try (InputStream in = new FileInputStream(apkPath); OutputStream out = session.openWrite("pokemon-xd.apk", 0, -1)) {
                    byte[] buffer = new byte[1 << 16];
                    int n;
                    while ((n = in.read(buffer)) > 0) {
                        out.write(buffer, 0, n);
                    }
                    session.fsync(out);
                }
                Intent intent = new Intent(activity, ApkInstaller.class).setAction(ACTION);
                PendingIntent pending = PendingIntent.getBroadcast(activity, id, intent, PendingIntent.FLAG_MUTABLE | PendingIntent.FLAG_UPDATE_CURRENT);
                session.commit(pending.getIntentSender());
            } finally {
                session.close();
            }
            return "";
        } catch (Exception e) {
            Log.e(TAG, "install failed", e);
            return String.valueOf(e);
        }
    }

    @Override
    public void onReceive(Context context, Intent intent) {
        int status = intent.getIntExtra(PackageInstaller.EXTRA_STATUS, PackageInstaller.STATUS_FAILURE);
        String message = intent.getStringExtra(PackageInstaller.EXTRA_STATUS_MESSAGE);
        Log.i(TAG, "status " + status + ": " + message);
        if (status == PackageInstaller.STATUS_PENDING_USER_ACTION) {
            Intent confirm = intent.getParcelableExtra(Intent.EXTRA_INTENT);
            if (confirm != null) {
                confirm.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
                context.startActivity(confirm);
            }
        }
    }
}
