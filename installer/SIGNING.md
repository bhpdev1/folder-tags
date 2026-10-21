# Signing and Microsoft Store submission

The Microsoft Store accepts Win32 installers only when the installer **and every PE file it
installs** are signed with a certificate from a CA in the Microsoft Trusted Root Program
(policy 10.2.9). FolderTags cannot ship as an MSIX instead: MSIX has no way to register icon
overlays or the *Tags* navigation pane.

The free route for an open source project is [SignPath Foundation](https://signpath.org/).
The CI side is ready ([release workflow](../.github/workflows/release.yml)); the steps below
are the maintainer's.

## 1. Apply to SignPath Foundation

1. Check the [conditions](https://signpath.org/terms): OSI license (GPLv3 ✓), public
   repository, no proprietary code, maintained and documented project, uninstaller provided
   (✓ setup), no data sent anywhere (✓ privacy policy in the README).
   The project must also be **already released in the form to be signed**: publish the
   unsigned setup in a GitHub release first (the workflow builds it without SignPath settings).
2. Turn on multi-factor authentication on GitHub, and later on SignPath (required for every
   team member).
3. Apply at <https://signpath.org/apply.html> with the repository URL. The README already
   contains the required *Code signing policy* section.

## 2. Configure SignPath (once accepted)

In the SignPath project created for FolderTags:

- Link the predefined **Trusted Build System for GitHub** to the project, and install the
  SignPath GitHub App on the repository if you want audit-log evaluation.
- Create two **artifact configurations**. `actions/upload-artifact` uploads a ZIP, so the root
  element is `<zip-file>`:

  `binaries`: the DLL and the uninstaller, both embedded in the setup
  ```xml
  <artifact-configuration xmlns="http://signpath.io/artifact-configuration/v1">
    <zip-file>
      <pe-file path="FolderTags.dll" product-name="FolderTags">
        <authenticode-sign />
      </pe-file>
      <!-- Inno Setup uninstaller, uninst-<version>-<hash>.e32 uploaded as .exe -->
      <pe-file path="uninst-*.exe">
        <authenticode-sign />
      </pe-file>
    </zip-file>
  </artifact-configuration>
  ```

  `setup`
  ```xml
  <artifact-configuration xmlns="http://signpath.io/artifact-configuration/v1">
    <zip-file>
      <pe-file path="FolderTags-Setup-*.exe">
        <authenticode-sign />
      </pe-file>
    </zip-file>
  </artifact-configuration>
  ```
  No `product-name` restriction on the setup and the uninstaller: Inno Setup pads its version
  strings with spaces (`"FolderTags"` is stored as 60 characters), which an exact match would
  reject.
  Check the exact syntax in the SignPath editor before saving; it validates the XML.
- Note the signing policy slug (usually `release-signing`, plus `test-signing` for trials).

## 3. Configure the GitHub repository

*Settings → Secrets and variables → Actions*:

| Kind | Name | Value |
|---|---|---|
| Secret | `SIGNPATH_API_TOKEN` | API token of a SignPath user with submitter rights |
| Variable | `SIGNPATH_ORGANIZATION_ID` | SignPath organization id |
| Variable | `SIGNPATH_PROJECT_SLUG` | project slug, e.g. `FolderTags` |
| Variable | `SIGNPATH_SIGNING_POLICY_SLUG` | `release-signing` (or `test-signing`) |
| Variable | `SIGNPATH_ENABLED` | `true` to sign on every `v*.*.*` tag |

## 4. Release

- Push a tag `v1.2.0` (or run *Actions → Release → Run workflow* with *sign* checked).
- Approve the two signing requests (DLL and uninstaller, then setup) in SignPath, each within
  an hour: the job fails after that.
- Download the `FolderTags-1.2.0` artifact and check the signatures:
  `Get-ChildItem -Recurse .\FolderTags-1.2.0 -File | Get-AuthenticodeSignature`.
  After installing, `C:\Program Files\FolderTags\unins000.exe` must be signed too.
- Attach the setup to the GitHub release. Its notes must mention the *Code signing policy*
  (a link to the README section is enough).

Without the SignPath settings the workflow still builds, and uploads `FolderTags-<version>-unsigned`.

## 5. Microsoft Store

In Partner Center (free individual account): *New product → EXE or MSI app*.

- Package URL: the HTTPS link of the signed setup on the GitHub release (versioned URL, the
  file must not change afterwards).
- Silent install switches: `/VERYSILENT /SUPPRESSMSGBOXES /NORESTART`
- Architecture: x64. Language: English and French.
- Installer handling ([Inno Setup exit codes](https://jrsoftware.org/ishelp/topic_setupexitcodes.htm)):
  `0` installation successful, `2` and `5` cancelled by user, `8` reboot required; `1`, `3`,
  `4` and `7` as custom failures pointing to that page.

## How the uninstaller gets signed

Inno Setup writes `unins000.exe` at install time from code inside the setup, so signing the
setup does not sign it. With `-SignedUninstallerDir`, `build-installer.ps1` turns on Inno
Setup's `SignedUninstaller` mode: the first compile writes the unsigned uninstaller as
`uninst-<version>-<hash>.e32` and stops (exit code 3); once that file is signed, the next
compile embeds its signature, and every installed `unins000.exe` carries it. The workflow
signs that file together with the DLL.
