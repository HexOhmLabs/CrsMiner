# CRSminer for Android v0.4 - Gradle Setup v3 (HOME error fixed)

[Ελληνική έκδοση](BUILD_FIX_README_GR.md)

Build-setup only; mining, UI, foreground service, widget, JNI/C++ and discovery sources remain identical to v0.4. Gradle 8.9, AGP 8.7.3 and the Android SDK requirements have not changed.

## What was wrong

The v2 PowerShell script used `foreach ($home in ...)` to check installed JDKs. PowerShell variable names are case-insensitive and `$HOME` is a protected built-in variable. The loop therefore stopped with `Cannot overwrite variable HOME because it is read-only or constant.`

The old `gradle-8.9-wrapper.jar` URLs at `services.gradle.org/distributions/` and `downloads.gradle.org/distributions/` return HTTP 404 because Gradle does **not** publish standalone wrapper JAR binaries there.

## v3 fix

- The JDK loop uses `$jdkHome` instead of the reserved `$HOME` variable.
- The script first retrieves the wrapper from the official Gradle source repository tag `v8.9.0` (`https://raw.githubusercontent.com/gradle/gradle/v8.9.0/gradle/wrapper/gradle-wrapper.jar`) and checks its SHA-256 against Gradle's published 8.9 checksum: `498495120a03b9a6ab5d155f5de3c8f0d986a449153702fb80fc80e134484f17`.
- If the GitHub download is unavailable, it searches for a JDK supported by Gradle 8.9 (prefer Java 17 or 21, including Android Studio's JBR), then uses a checksum-verified Gradle distribution to generate the wrapper.
- It will not silently run Gradle 8.9 using Java 25.

## Steps (Windows)

Option 1: Extract the **repair-only ZIP** into your existing `CRSminer_for_Android` directory, preserving the `tools` subfolder and overwriting the two setup files. Run `PREPARE_WINDOWS.bat` again. Check that its first title says `Gradle setup v3 HOME fix`.

Option 2: Unzip the **complete v0.4 package** into an empty directory and run `PREPARE_WINDOWS.bat` inside the `CRSminer_for_Android` project root.

After successful preparation, set Android Studio's **Gradle JDK to Java 17 or 21** under Settings > Build, Execution, Deployment > Build Tools > Gradle. Then sync and build.

The ZIP and updated script have been checked structurally, but this Windows-only script has not been run on your PC.

## References

- https://github.com/gradle/gradle/blob/v8.9.0/gradle/wrapper/gradle-wrapper.jar
- https://gradle.org/release-checksums/
- https://docs.gradle.org/current/userguide/gradle_wrapper.html
