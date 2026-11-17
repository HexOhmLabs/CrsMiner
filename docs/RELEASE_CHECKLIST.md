# First GitHub publication checklist

1. Create a public GitHub repository `CRSminer` under the **HexOhm** account, with no template README/license (they are in this package).
2. Unzip and upload the **contents of the `CRSminer_GitHub_Final_Publication` folder**, preserving paths and the hidden `.github` directory.
3. Review the README and licensing; check no credentials or private data were added locally.
4. Create a GitHub **Release** (suggested name `CRSminer — Initial Public Release`), attach the two renamed merged binaries as convenient download assets, and link to `docs/FLASHING.md` for separate component flashing. Add a link to the **[Raspberry Pi build instructions](../monitoring/Raspberry_Pi_USB_ASIC/README.md)**: the Raspberry Pi uses the four SD-card boot files, **not a `.bin`**.
5. Keep the published hardware list limited to the working builds approved by the project owner.
6. Try a fresh USB flash on a spare board **before** labeling binaries generally tested for others. Keep a backup of any live miner configuration.
7. Provide support through GitHub Issues; ask users to exclude private keys, Wi-Fi passwords, pool credentials and personal network identifiers from screenshots/logs.

No files in this archive have been committed or published by the assistant.
