CRS Mining - fix for HTTP 404 in PREPARE_WINDOWS.bat

The previous script incorrectly tried to download a wrapper JAR from the
Gradle distributions server. Gradle supplies a checksum there, but not the
JAR binary. This version retrieves a GitHub copy of the wrapper and accepts it only if
its SHA-256 matches one of Gradle's official published release checksums.
The wrapper JAR itself may be from an earlier official Gradle version; it
can still launch the configured Gradle 9.5.0 distribution.

STEPS
1. Extract this ZIP into a fresh Windows folder, e.g. C:\AndroidProjects.
2. Open CRS_Mining_Android and double-click PREPARE_WINDOWS.bat.
3. Look for SUCCESS: Official Gradle wrapper verified by SHA-256.
4. Then open CRS_Mining_Android in Android Studio and sync Gradle.

This package does not change any application Java files, AndroidManifest,
resources, or the build-tool versions. It only repairs the bootstrap script.

NOTE: The Windows downloading step could not be executed in the environment
that created this package. If you receive an error, copy all of its text.
