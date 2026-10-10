package com.conningtower.shgame;

import android.app.Activity;
import android.content.ActivityNotFoundException;
import android.content.ContentResolver;
import android.content.Intent;
import android.database.Cursor;
import android.net.Uri;
import android.os.ParcelFileDescriptor;
import android.provider.DocumentsContract;
import android.util.Log;
import java.io.FileNotFoundException;
import java.util.ArrayList;
import org.libsdl.app.SDLActivity;

/**
 * The game's activity: SDL's, with SDL linked statically into libmain.so
 * (the game) rather than loaded as libSDL3.so. It also gives the native
 * asset setup (app/install_setup.h) the system folder picker and read
 * access to the folder chosen through the Storage Access Framework
 * (app/android_documents.h). These static methods are called from native
 * threads.
 */
public class GameActivity extends SDLActivity {
  private static final String TAG = "GameActivity";
  private static final int PICK_FOLDER_REQUEST = 0x5348;

  private static final Object pickLock = new Object();
  private static boolean pickDone;
  private static String pickResult;

  @Override
  protected String[] getLibraries() {
    return new String[] {"main"};
  }

  /**
   * Shows the system folder picker and waits until it is closed. Returns the
   * tree URI chosen, with a persisted read permission (kept across restarts),
   * or null when the picker is cancelled or unavailable.
   */
  public static String pickFolder() throws InterruptedException {
    final Activity activity = getContext();
    synchronized (pickLock) {
      pickDone = false;
      pickResult = null;
    }
    activity.runOnUiThread(new Runnable() {
      @Override
      public void run() {
        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE);
        intent.addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION | Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION);
        try {
          activity.startActivityForResult(intent, PICK_FOLDER_REQUEST);
        } catch (ActivityNotFoundException e) {
          Log.e(TAG, "no folder picker", e);
          finishPick(null);
        }
      }
    });
    synchronized (pickLock) {
      while (!pickDone) {
        pickLock.wait();
      }
      return pickResult;
    }
  }

  private static void finishPick(String uri) {
    synchronized (pickLock) {
      pickResult = uri;
      pickDone = true;
      pickLock.notifyAll();
    }
  }

  @Override
  protected void onActivityResult(int requestCode, int resultCode, Intent data) {
    if (requestCode != PICK_FOLDER_REQUEST) {
      super.onActivityResult(requestCode, resultCode, data);
      return;
    }
    Uri uri = resultCode == RESULT_OK && data != null ? data.getData() : null;
    if (uri == null) {
      finishPick(null);
      return;
    }
    try {
      getContentResolver().takePersistableUriPermission(uri, Intent.FLAG_GRANT_READ_URI_PERMISSION);
    } catch (SecurityException e) {
      // The folder stays readable until the app ends; the next start finds
      // it unreadable and runs the setup again.
      Log.w(TAG, "no persistable permission for " + uri, e);
    }
    finishPick(uri.toString());
  }

  /** Returns the document id of the root of tree `treeUri`, null if it is not a tree URI. */
  public static String documentTreeRoot(String treeUri) {
    try {
      return DocumentsContract.getTreeDocumentId(Uri.parse(treeUri));
    } catch (IllegalArgumentException e) {
      Log.w(TAG, "not a document tree: " + treeUri, e);
      return null;
    }
  }

  /**
   * Returns the children of directory `docId` of tree `treeUri`, four strings
   * each: name, document id, "d" for a directory or "f" for a file, size in
   * bytes. Returns null when the directory cannot be listed (the permission
   * was revoked, the folder removed).
   */
  public static String[] listDocuments(String treeUri, String docId) {
    Uri tree = Uri.parse(treeUri);
    Uri children = DocumentsContract.buildChildDocumentsUriUsingTree(tree, docId);
    String[] columns = {
      DocumentsContract.Document.COLUMN_DISPLAY_NAME,
      DocumentsContract.Document.COLUMN_DOCUMENT_ID,
      DocumentsContract.Document.COLUMN_MIME_TYPE,
      DocumentsContract.Document.COLUMN_SIZE,
    };
    ContentResolver resolver = getContext().getContentResolver();
    try (Cursor cursor = resolver.query(children, columns, null, null, null)) {
      if (cursor == null) {
        return null;
      }
      ArrayList<String> out = new ArrayList<>();
      while (cursor.moveToNext()) {
        boolean directory = DocumentsContract.Document.MIME_TYPE_DIR.equals(cursor.getString(2));
        out.add(cursor.getString(0));
        out.add(cursor.getString(1));
        out.add(directory ? "d" : "f");
        out.add(cursor.isNull(3) ? "0" : Long.toString(cursor.getLong(3)));
      }
      return out.toArray(new String[0]);
    } catch (SecurityException | IllegalArgumentException e) {
      Log.w(TAG, "cannot list " + children, e);
      return null;
    }
  }

  /**
   * Opens document `docId` of tree `treeUri` for reading. Returns its file
   * descriptor, owned by the caller, or -1 when it cannot be opened.
   */
  public static int openDocument(String treeUri, String docId) {
    Uri document = DocumentsContract.buildDocumentUriUsingTree(Uri.parse(treeUri), docId);
    try {
      ParcelFileDescriptor pfd = getContext().getContentResolver().openFileDescriptor(document, "r");
      return pfd == null ? -1 : pfd.detachFd();
    } catch (FileNotFoundException | SecurityException | IllegalArgumentException e) {
      Log.w(TAG, "cannot open " + document, e);
      return -1;
    }
  }
}
