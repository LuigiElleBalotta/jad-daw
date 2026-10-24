# Code signing (Windows, through SignPath)

The release workflow (`.github/workflows/release.yml`) signs the Windows programs when the repository has the SignPath secret; without it the release is built unsigned, as before. Nothing else has to change.

## What gets signed

1. `jad-daw.exe` and `lpc-plugin-scanner.exe` (artifact configuration `exe`), before they go into the zip and the setup.
2. `jad-daw-<version>-windows-x64-setup.exe` (artifact configuration `installer`), after Inno Setup has built it.

The Qt and FFmpeg DLLs keep the signatures their makers gave them or none.

## One-time setup

1. Apply for the free open-source plan at https://signpath.org/ (SignPath Foundation). The repository is public and AGPL-3.0, which is what they ask for. They need the project name, the repository link and an account that is able to approve releases. Approval takes some days.
2. In SignPath create a project (its *slug* goes below) with **two artifact configurations**. In both the artifact is a ZIP of the files at its root:
   - slug `exe`: sign `jad-daw.exe` and `lpc-plugin-scanner.exe` (Authenticode).
   - slug `installer`: sign `jad-daw-*-setup.exe` (Authenticode).
3. Add a signing policy (for example `release-signing`) and connect the project to GitHub: the trusted build system of SignPath must be the GitHub Actions of this repository, so that only workflow runs of it can request signatures. The policy can require a manual approval for each request, which is wise.
4. In GitHub, Settings > Secrets and variables > Actions:
   - secret `SIGNPATH_API_TOKEN`: the API token of a SignPath user that may submit to the project.
   - variables `SIGNPATH_ORGANIZATION_ID`, `SIGNPATH_PROJECT_SLUG`, `SIGNPATH_SIGNING_POLICY_SLUG`.
5. Push a version tag. The build job waits for each signing request (`wait-for-completion`), so with manual approval the run waits until someone approves in SignPath.

## What this does not do

- macOS is not signed: that needs an Apple Developer ID (99 USD a year) and notarization, which SignPath does not offer. Until then the first start needs right-click > Open, and the updater cannot rely on the app opening without that.
- SmartScreen still warns for a while: with a normal (not EV) certificate its reputation builds up over downloads.
- The workflow was written from the documentation of the SignPath action and was not run (no account yet): check the first signed run, the names of the steps' inputs in particular.
