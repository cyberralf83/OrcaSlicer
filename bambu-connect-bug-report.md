## Bug Report: Bambu Connect v2.2.0-beta.1 — Upload fails with 403 when file is imported via URI scheme

### Summary

Files imported via the `bambu-connect://import-file` URI scheme fail to upload to the printer with a **403 Forbidden** error in Bambu Connect v2.2.0-beta.1. The same file imported via drag-and-drop uploads and prints successfully.

### Steps to Reproduce

1. Slice a model in OrcaSlicer (or any slicer that exports `.3mf` files)
2. Open the `.3mf` file in Bambu Connect via the URI scheme, e.g.:
   ```
   bambu-connect://import-file?path=%2FUsers%2Fuser%2FDownloads%2Fmodel.3mf&name=model&version=1.0.0
   ```
   (This can be triggered from a terminal with `open "bambu-connect://import-file?path=..."` on macOS)
3. Bambu Connect opens and shows the "Import file" dialog — click **"Import Gcode 3MF"**
4. The file imports successfully and navigates to the print screen
5. Select a printer and click **Print** (cloud print)
6. **Result:** Upload fails with 403 Forbidden

### Expected Behavior

The file uploads and the print starts — as it does when the same `.3mf` file is added via drag-and-drop or file picker.

### Affected Version

- **Broken:** Bambu Connect v2.2.0-beta.1
- **Working:** Bambu Connect v2.0.0-beta.7

### Root Cause

In the URI import path (the import file dialog handler), the file read from disk via IPC is reconstructed as:

```js
new File(
    [new Blob([fileData], { type: "application/octet-stream" })],
    `${name}.3mf`,
    { type: "text/plain" }  // <-- incorrect MIME type
)
```

The outer `File` is created with `type: "text/plain"`. When this file is later uploaded via `XMLHttpRequest.send(file)`, the browser sets `Content-Type: text/plain` on the PUT request to the presigned upload URL. The presigned URL rejects this with **403 Forbidden** because the content type doesn't match what the signature expects.

In contrast, files added via drag-and-drop are native browser `File` objects with an empty or correct MIME type, so the upload succeeds.

### Why this wasn't visible in v2.0.0

The v2.0.0 upload function resolved the promise on **any** HTTP status (including 403), silently ignoring the error. The v2.2.0 upload function correctly checks for HTTP 200 and rejects on other statuses, surfacing the previously hidden error.

### Suggested Fix

Change the File constructor in the URI import handler from:
```js
{ type: "text/plain" }
```
to:
```js
{ type: "application/octet-stream" }
```

This matches the MIME type used for the inner Blob and is consistent with what native drag-and-drop File objects would use for `.3mf` files.
