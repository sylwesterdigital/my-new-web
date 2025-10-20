## Issues on Mac?

<img width="372" height="362" alt="Screenshot 2025-10-20 at 14 54 44" src="https://github.com/user-attachments/assets/aba8551b-47c7-4abd-85ac-4444d174d76f" />

The dialog is misleading. Gatekeeper uses the same “is damaged and can’t be opened” text for multiple cases (unsigned, altered-after-signing, or not notarized), which can imply network corruption when the file is intact.

Two practical things to neutralize this for your users right now:

## 1) Add a clear note to your release

```
macOS may show “<App> is damaged and can’t be opened.” 
This is Gatekeeper blocking an unsigned/not-notarized app (not a corrupted download).
Workaround: Right-click → Open (once), or clear quarantine:
xattr -dr com.apple.quarantine /path/to/Tiny\ Text\ Client.app
```

## 2) Ship notarized builds so the dialog never appears

Put this into your release script (after the app is built, before zipping/uploading). Replace TEAMID and identity:

```zsh
APP="dist/Tiny Text Client.app"
ZIP="dist/Tiny_Text_Client.zip"
IDENTITY="Developer ID Application: Your Name (TEAMID)"

# sign (hardened runtime)
codesign --force --deep --options runtime --sign "$IDENTITY" "$APP"

# zip for notarization
ditto -c -k --keepParent "$APP" "$ZIP"

# one-time: xcrun notarytool store-credentials "TTSProfile" --apple-id you@example.com --team-id TEAMID --password app-specific-password
xcrun notarytool submit "$ZIP" --keychain-profile "TTSProfile" --wait

# staple & verify
xcrun stapler staple "$APP"
spctl -a -vv "$APP"
xcrun stapler validate "$APP"
```

## If a user wants to verify what Gatekeeper is blocking

```zsh
spctl -a -vv /path/to/Tiny\ Text\ Client.app
# and check quarantine flags:
xattr -p com.apple.quarantine /path/to/Tiny\ Text\ Client.app 2>/dev/null || echo "no quarantine"
```

## Optional distribution paths that avoid confusion

* Homebrew cask (signed/notarized ZIP).
* Sparkle updates for future versions (signed feed).

The message text can’t be changed by apps, so the most effective mitigation is (a) notarize releases and (b) explain it plainly in the release notes.
