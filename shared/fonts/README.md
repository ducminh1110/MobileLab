# Fonts

All MobileLab interfaces use **Inter** for text and **JetBrains Mono** for code, logs and identifiers.
San Francisco (and SF Mono) are deliberately not used: they may only be used on Apple platforms and are
not available on Linux or in a browser.

| Font | Files | License |
| --- | --- | --- |
| Inter 4.0 | `Inter-Regular/Medium/SemiBold/Bold.ttf` | SIL OFL 1.1, see `LICENSE-Inter.txt` |
| JetBrains Mono 2.304 | `JetBrainsMono-Regular/Medium/Bold.ttf` | SIL OFL 1.1, see `OFL-JetBrainsMono.txt` |

These TTFs are bundled by the native apps (macOS and Linux). The web dashboard serves WOFF2 builds of the
same fonts from `backend/public/assets/fonts/`, because its content-security policy only allows same-origin
requests.
