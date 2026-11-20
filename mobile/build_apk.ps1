# Build an installable APK. Needs Flutter SDK + Android SDK. Run from the mobile\ folder.
$ErrorActionPreference = "Stop"
if (-not (Test-Path android)) { flutter create . --platforms=android --project-name trashnet --org vn.trashnet }
# one-time: add permissions + cleartext (see android_overrides\AndroidManifest.permissions.xml)
flutter pub get
flutter analyze
flutter test
flutter build apk --release --split-per-abi
Write-Host "APK: build\app\outputs\flutter-apk\app-arm64-v8a-release.apk (copy to phone and install)"
