# How to put this on GitHub

This `ship` folder has two parts:

| Folder | What it is | Where it goes |
|---|---|---|
| `repo` | the source code, README, license, build scripts and patches | the GitHub **repository** |
| `release` | `SpyroRecomp-{VERSION}.zip`: what players download (launcher, game program, shader cache) | a GitHub **Release** (not into the repository) |

Neither part contains any game files (no ISO, no default.xex, no game data).

## 1. Make the repository (website, no command line needed)

1. Sign in at https://github.com, click **+** (top right) > **New repository**.
2. Name it, e.g. `spyro-dotd-recomp`. Choose **Public**. Do **not** tick "Add a README"
   (we have one). Click **Create repository**.
3. On the empty repository page click **uploading an existing file**.
4. Open the `ship\repo` folder in Explorer, select everything inside it (Ctrl+A) and drag it into
   the browser. Hidden files: `.gitignore` is included; if Explorer hides it, turn on
   *View > Show > Hidden items* first.
5. Write a message such as "Pre-alpha 1" and click **Commit changes**.

(If you prefer the command line or GitHub Desktop: initialise a git repository inside `ship\repo`,
commit everything, and push it to the new repository.)

## 2. Publish the release

1. On the repository page, click **Releases** (right side) > **Create a new release**.
2. **Choose a tag**: type `{VERSION}` and click *Create new tag*.
3. Title: `Pre-alpha 1`. Tick **Set as a pre-release**.
4. Description: copy the short text from `release\RELEASE_NOTES.md`.
5. Drag `release\SpyroRecomp-{VERSION}.zip` into the "Attach binaries" box and wait for the upload.
6. Click **Publish release**.

## Making the next version

Run `project\make-ship.ps1 -Version v0.2.0-prealpha` from the project; it rebuilds this
`ship` folder from the current build. Upload the changed files from `ship\repo` and make a new
release with the new zip.
