# Release assets

GitHub does not allow files larger than 100 MB inside a repository. This folder is where those files sit locally, ready to be attached to a [GitHub Release](https://github.com/rohaan2802/Multithreading/releases). The binaries in this folder are listed in `.gitignore` and are not committed. This README is the only file from here that is tracked.

Do not create or publish the release from a script — upload the file yourself from GitHub (or GitHub Desktop) when you are ready.

## What goes here

| File | Size | What it is | Where it is used |
|------|------|------------|------------------|
| `SUSY.zip` | ~799 MB | [UCI SUSY](https://archive.ics.uci.edu/dataset/279/susy) dataset archive | Task 1 — logistic-style batch scoring |

After you publish a release (for example `v1.0`), the download link will be:

`https://github.com/rohaan2802/Multithreading/releases/download/v1.0/SUSY.zip`

If `SUSY.zip` is not already in this folder, get it from the UCI page above, or, if you still have a clone from before the file was removed from the tree, restore it with Git LFS from that earlier commit and copy it here.

The two smaller dataset archives (`datasets/webbase-1M.tar.gz` and `datasets/web-Google.txt.gz`) stay in the source tree because they are well under the 100 MB limit. Assignment PDFs and the result workbooks are also in the tree — they are small.
