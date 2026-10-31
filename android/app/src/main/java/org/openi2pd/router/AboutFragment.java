/*
 * See the LICENSE file at the top of the project tree for copyright and
 * license details.
 *
 * openi2pd: AboutFragment.
 *
 * Renders application identity: version name and code, device ABI list,
 * build type, the app-specific daemon data directory and a license
 * summary, plus buttons that open the repository and license pages.
 * Failed package lookups or browser launches degrade to placeholders
 * and a toast instead of crashing.
 *
 */

package org.openi2pd.router;

import android.content.Context;
import android.content.Intent;
import android.content.pm.ApplicationInfo;
import android.content.pm.PackageInfo;
import android.content.pm.PackageManager;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.text.TextUtils;
import android.view.LayoutInflater;
import android.view.View;
import android.view.ViewGroup;
import android.widget.TextView;
import android.widget.Toast;

import androidx.annotation.NonNull;
import androidx.annotation.Nullable;
import androidx.core.content.pm.PackageInfoCompat;
import androidx.fragment.app.Fragment;

import java.io.File;

/**
 * Application identity and license information. No secrets, keys or
 * native pointers are displayed. The ABI shown is the device ABI; the
 * daemon data directory follows the application-specific storage
 * layout.
 */
public class AboutFragment extends Fragment {
    private static final String DATA_DIRECTORY_NAME = "open-i2pd";

    @Nullable
    @Override
    public View onCreateView(@NonNull LayoutInflater inflater, @Nullable ViewGroup container,
            @Nullable Bundle savedInstanceState) {
        return inflater.inflate(R.layout.fragment_about, container, false);
    }

    @Override
    public void onViewCreated(@NonNull View view, @Nullable Bundle savedInstanceState) {
        super.onViewCreated(view, savedInstanceState);
        Context context = requireContext();

        TextView version = view.findViewById(R.id.text_app_version);
        TextView abi = view.findViewById(R.id.text_native_abi);
        TextView buildType = view.findViewById(R.id.text_build_type);
        TextView dataDirectory = view.findViewById(R.id.text_data_directory);
        TextView license = view.findViewById(R.id.text_license);

        version.setText(versionText(context));
        abi.setText(abiText());
        buildType.setText(
                isDebuggable(context) ? R.string.about_build_debug : R.string.about_build_release);
        dataDirectory.setText(dataDirectory(context));
        license.setText(R.string.about_license_summary);

        view.findViewById(R.id.button_open_repository)
                .setOnClickListener(ignored -> openUrl(getString(R.string.repository_url)));
        view.findViewById(R.id.button_open_license)
                .setOnClickListener(ignored -> openUrl(getString(R.string.license_url)));
    }

    @NonNull
    private String versionText(@NonNull Context context) {
        try {
            PackageInfo info =
                    context.getPackageManager().getPackageInfo(context.getPackageName(), 0);
            String name = TextUtils.isEmpty(info.versionName)
                    ? context.getString(R.string.value_unknown)
                    : info.versionName;
            return context.getString(R.string.about_version_format, name,
                    PackageInfoCompat.getLongVersionCode(info));
        } catch (PackageManager.NameNotFoundException | RuntimeException exception) {
            return context.getString(R.string.value_unknown);
        }
    }

    @NonNull
    private String abiText() {
        String[] abis = Build.SUPPORTED_ABIS;
        if (abis == null || abis.length == 0) {
            return getString(R.string.value_unknown);
        }
        return TextUtils.join(", ", abis);
    }

    private static boolean isDebuggable(@NonNull Context context) {
        ApplicationInfo info = context.getApplicationInfo();
        return info != null && (info.flags & ApplicationInfo.FLAG_DEBUGGABLE) != 0;
    }

    @NonNull
    private String dataDirectory(@NonNull Context context) {
        File base = context.getExternalFilesDir(null);
        if (base == null) {
            base = context.getFilesDir();
        }
        if (base == null) {
            return getString(R.string.about_data_unavailable);
        }
        return new File(base, DATA_DIRECTORY_NAME).getAbsolutePath();
    }

    private void openUrl(@NonNull String url) {
        Intent intent = new Intent(Intent.ACTION_VIEW, Uri.parse(url));
        intent.addCategory(Intent.CATEGORY_BROWSABLE);
        try {
            startActivity(intent);
        } catch (RuntimeException exception) {
            Toast.makeText(requireContext(), R.string.no_browser_available, Toast.LENGTH_SHORT)
                    .show();
        }
    }
}
