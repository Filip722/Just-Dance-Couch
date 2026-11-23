# Supported Wii disc images

Build any of the 18 editions below from your own Wii disc backup. Just Dance Couch reads ISO, RVZ, and WBFS images. For help making a backup on a Wii, see [CleanRip](https://github.com/emukidid/cleanrip).

Use the matching disc ID, region, and revision. Most entries use USA revision 0. Just Dance 3 uses USA revision 1, and Just Dance 2020 uses Europe revision 0.

The **SHA-256** column is for the extracted `sys/main.dol` executable. The builder checks it after extraction along with the disc ID, region, and revision. Whole-image hashes vary across ISO, RVZ, and WBFS containers.

| Edition | Disc ID | Region | Revision | SHA-256 of `sys/main.dol` |
| --- | --- | --- | --- | --- |
| Just Dance | `SDNE41` | USA | 0 | `6938c96e3511568eec2c2abaaa26b64cbd47a852b51dbf20929da053a2c8beb1` |
| Just Dance 2 | `SD2Y41` | USA | 0 | `ac002beb4985e7ba7f75708b558e0a78480def4a2b4e2e0152c5d3340de57520` |
| Just Dance Kids | `SDZE41` | USA | 0 | `456e5cfe987b212a3215cc6abe90b6982c841839da2272155efdc73ee5fb9619` |
| Just Dance Summer Party | `SJ9E41` | USA | 0 | `e6728082fd351ac90503cfec6684b4515ef0bf5ae9a766060de59fe2d59b0ee0` |
| Just Dance 3 | `SJDE41` | USA | 1 | `18c218dd67a4c1d8f05ad40a380bc1822c3ab4a4ff7d04a24d043f933997acf7` |
| Just Dance Kids 2 | `SJZE41` | USA | 0 | `35f046d2f911fd8512c9099e3204caeaee3ef628d40ff5418405cb31b50f0411` |
| Just Dance Greatest Hits | `SJHE41` | USA | 0 | `d74f6d36712807084c8b71033850bde4a7778ef8662722deec5309a0050fbd87` |
| Just Dance 4 | `SJXE41` | USA | 0 | `2b55c4e869e15d89f705fcc76912ea800f73167fbe49437c0e9e1f9756ffc0da` |
| Just Dance Disney Party | `SJ6E41` | USA | 0 | `5c67b9d8a0fa7b61225861b8bf823ae43b5cb10900166579b7d879eb8d1ff0b1` |
| Just Dance 2014 | `SJOE41` | USA | 0 | `4d60b98e73de8052c20d52df026a99994186aa0c618072423e3f1007a42e8d3d` |
| Just Dance Kids 2014 | `SJ7E41` | USA | 0 | `d7a4e53c8ba8a40c911704377d5399ab59b907f459b0fdece0a45c574b69fa75` |
| Just Dance 2015 | `SE3E41` | USA | 0 | `3780fa7d05f45cf7a90d95ac7368a9c4276168d8c339a3418d8ff06e8c60dfae` |
| Just Dance 2016 | `SJNE41` | USA | 0 | `63d052ce99b41944e397ee518e3b33e8e16965b320e3d801a8e7a0022cdab264` |
| Just Dance Disney Party 2 | `S5DE41` | USA | 0 | `6ec3d07053bd2951e4f77dd27e46de366e45b3c9d5204b4dd7eba347fc3de162` |
| Just Dance 2017 | `SZ7E41` | USA | 0 | `fa7e5cda4da2a1ddafcc71f32bfaa1a4acca311dcafde939b247b49bb9896d16` |
| Just Dance 2018 | `SE8E41` | USA | 0 | `fefc8746c08f91f3e19a7fefca4e482b69e11bd88f06b1bdacfa7a209ee5d312` |
| Just Dance 2019 | `S5EE41` | USA | 0 | `0c30ff85d33c38fa933bdd3ff1af7de01d97b43ded7a36610cadb2c8e009ec84` |
| Just Dance 2020 | `S2UP41` | Europe | 0 | `17a6b56790c79d5a49b6fc28569247fef35becca75cbd8df00867248119e874d` |

## Use a different location or filename

Pass the full path to your disc image if your backup lives on another drive.

```bash
python3 -B pilot.py build --disc SJOE41 --image "/path/to/drive/Just Dance 2014.rvz"
python3 -B pilot.py couch
```

Without `--image`, the builder looks in `~/Wii/Just Dance` or the directory set by `JUST_DANCE_BACKUPS`. It uses the edition names above with the `.rvz` extension. For Just Dance 2020 it looks for `Just Dance 2020 (Europe) (En,Fr,De,Es,It,Nl).rvz`.

## Check an executable hash

The build verifies the executable automatically. To check the hash yourself, extract the image with nodtool into a temporary directory and run `sha256sum` on its `sys/main.dol` file.

```bash
(
  set -e
  temp=$(mktemp -d)
  trap 'rm -r -- "$temp"' EXIT
  nodtool extract --quiet "/path/to/Just Dance 2014.rvz" "$temp/disc"
  sha256sum "$temp/disc/sys/main.dol"
)
```

[Back to the quick start](../README.md#quick-start)
