/*
 * See the LICENSE file at the top of the project tree for copyright
 * and license details.
 * openi2pd: out-of-band SAF cancellation IPC regressions for manual device CI.
 */
package org.openi2pd.router;

import static org.junit.Assert.*;
import android.os.Binder;
import android.os.IBinder;
import android.os.Parcel;
import androidx.test.ext.junit.runners.AndroidJUnit4;
import org.junit.Test;
import org.junit.runner.RunWith;

@RunWith(AndroidJUnit4.class)
public final class TorrentSafCancellationWireTest {
    @Test
    public void cancellationNeverWaitsForReplyParcel() throws Exception {
        final long[] received = new long[1];
        final int[] calls = new int[1];
        Binder endpoint = new Binder() {
            @Override
            protected boolean onTransact(int code, Parcel data, Parcel reply, int flags) {
                data.enforceInterface("org.openi2pd.router.ITorrentSafStorage");
                assertEquals(IBinder.FLAG_ONEWAY, flags & IBinder.FLAG_ONEWAY);
                assertNull(reply);
                if (calls[0]++ == 0)
                    received[0] = data.readLong();
                return true;
            }
        };
        // An un-attached Binder deliberately forces the generated remote proxy path.
        ITorrentSafStorage proxy = ITorrentSafStorage.Stub.asInterface(endpoint);
        proxy.cancel(74231L);
        proxy.retire();
        assertEquals(74231L, received[0]);
        assertEquals(2, calls[0]);
    }
}
