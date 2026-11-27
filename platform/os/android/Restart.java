package net.pokemonxdpc.port;

import android.app.Activity;
import android.content.Intent;
import android.os.Bundle;
import android.os.Process;
import android.util.Log;

/**
 * Starts the app again (docs/ANDROID.md "The game starts again"): a New Game setup that needs other mods and a soft reset make the game start
 * again, which on a PC is a new program. A phone app cannot start another program, and a process that only exits is not opened again. This
 * screen lives in a process of its own (manifest: android:process=":restart"): the game opens it, it ends the game's process, then opens the
 * app's front door again (a new process, which reads the settings the game left in files/xd_restart_env.txt). Called from native code.
 */
public class Restart extends Activity {
    private static final String TAG = "XDrestart";

    /** Called by the game (platform/os/android_restart.cpp): opens the helper screen; returns "" when it was asked for, else why not. */
    public static String begin(Activity activity) {
        try {
            Intent intent = new Intent(activity, Restart.class);
            intent.putExtra("pid", Process.myPid());
            intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_NO_ANIMATION);
            activity.startActivity(intent);
            return "";
        } catch (Exception e) {
            Log.e(TAG, "could not open the restart screen", e);
            return String.valueOf(e);
        }
    }

    @Override
    protected void onCreate(Bundle state) {
        super.onCreate(state);
        int pid = getIntent().getIntExtra("pid", 0);
        if (pid > 0 && pid != Process.myPid()) {
            Process.killProcess(pid);
        }
        try {
            Thread.sleep(300);
        } catch (InterruptedException e) {
            // carry on
        }
        Intent launch = getPackageManager().getLaunchIntentForPackage(getPackageName());
        if (launch != null) {
            launch.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_CLEAR_TASK);
            startActivity(launch);
        } else {
            Log.e(TAG, "no launch intent for " + getPackageName());
        }
        finish();
        // this helper's own process ends too (the next start is a clean one)
        Process.killProcess(Process.myPid());
    }
}
