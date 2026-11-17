# First GitHub publication checklist

1. Use the existing public GitHub repository `HexOhmLabs/CrsMiner`; preserve the existing README and LICENSE when updating.
2. Upload the **contents** of this revised repository ZIP, preserving the root README, directory structure and hidden `.github` directory. For an incremental upload use the companion patch ZIP instead of replacing unrelated files.
3. Review the README and licensing; check no credentials or private data were added locally.
4. Create a GitHub **Release** (suggested name `CRSminer — Initial Public Release`), attach the two renamed merged binaries as convenient download assets, and link to `docs/FLASHING.md` for separate component flashing. Add a link to the **[Raspberry Pi build instructions](../firmware/Raspberry_Pi_USB_ASIC/README.md)**: the Raspberry Pi uses the four SD-card boot files, **not a `.bin`**.
5. Include the separately packaged **CRSminer for Android CPU miner** source, and keep it clearly distinct from the Android Monitor. The user-supplied Android CPU miner APK is a debug build, supplied for developer testing only; create and verify a separately signed release APK before distributing a production installation. Publish the working Android Monitor **v1.2** as recommended, and preserve **v1.3** as optional source for independent development/testing, not as owner-validated.
6. Try a fresh USB flash on a spare board **before** labeling binaries generally tested for others. Keep a backup of any live miner configuration.
7. Provide support through GitHub Issues; ask users to exclude private keys, Wi-Fi passwords, pool credentials and personal network identifiers from screenshots/logs.

Check the live GitHub repository after upload to confirm that links, images and the recommended-version labels render correctly.
