# Putting the Eighth Hall page on marlenafabrick.net

The page lives in `website/eighth-hall/` and is plain HTML/CSS/JavaScript, with no
WordPress or build step. Visitors can play the four sample sounds (or their own
audio file) through the real reverb engine, running in their browser.

## Upload with Hostinger's File Manager

1. Log in to Hostinger -> **Websites** -> **Manage** (marlenafabrick.net) -> **File Manager**.
2. Open the **public_html** folder.
3. Create a new folder called **eighth-hall**.
4. Open it and **upload everything inside** `website/eighth-hall/` from this project:
   - `index.html`
   - `reverb-engine.js`
   - `playground.js`
   - the `images` folder
   - the `samples` folder
   (Upload the folders' contents into matching `images` and `samples` folders, or
   zip `eighth-hall`, upload the zip, and use File Manager's **Extract**.)
5. Visit **https://marlenafabrick.net/eighth-hall/**.
6. Add a link to it from your portfolio's home page, e.g.
   `<a href="/eighth-hall/">Eighth Hall - reverb plugin</a>`.

## Notes

- **HTTPS is recommended.** Over HTTPS the reverb runs on the browser's dedicated
  audio thread. Over plain HTTP it still works (it falls back automatically) but
  may glitch on slow phones. Hostinger includes free SSL: **Websites -> Manage ->
  Security -> SSL**.
- **Opening `index.html` by double-clicking it won't play sound.** Browsers block
  audio files loaded straight from disk. To preview locally, use the VS Code
  "Live Server" extension, or run `python -m http.server` in the folder and open
  http://localhost:8000.
- To change or add sounds, edit `tools/make_web_samples.py`, or replace the files in
  `samples/` (and update the `SAMPLES` list at the top of `playground.js`).
- The download buttons point to GitHub Releases, so they work once you've pushed
  the project and tagged `v1.0.0` (see BUILDING.md).
